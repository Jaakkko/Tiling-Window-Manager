//
// Created by jaakko on 10.4.2020.
//

#include "instance.h"
#include "logger.h"
#include "input.h"
#include "events.h"
#include "util.h"
#include "config.h"
#include "tree.h"
#include "bar/bar.h"

#include <stdlib.h>
#include <string.h>

#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

int wmSkipNextEnterNotify = 0;
int wmMouseX;
int wmMouseY;

unsigned wmActiveWorkspace = 0;

wmDialog* wmDialogs = NULL;

wmWindow* wmHead = NULL;
wmWindow* wmTail = NULL;

wmSplitMode wmSplitOrientation = NONE;

wmWorkspace wmWorkspaces[WORKSPACE_COUNT];

int wmRunning = True;
int wmExitCode = 0;

static Window wmcheckwin;
static wmWindow* fullscreen = NULL;

static void addAtom(Window window, Atom property, Atom value, unsigned maxAtomsCount) {
    Atom type_return;
    int format_return;
    unsigned long nitems;
    unsigned long bytes_after_return;
    unsigned long* prop_return;
    Status status = XGetWindowProperty(
            wmDisplay,
            window,
            _NET_WM_STATE,
            0,
            maxAtomsCount,
            False,
            AnyPropertyType,
            &type_return,
            &format_return,
            &nitems,
            &bytes_after_return,
            (unsigned char**)&prop_return
    );
    if (status != Success) {
        return;
    }

    int exist = 0;
    int i;
    for (i = 0; i < nitems; i++) {
        if (prop_return[i] == value) {
            exist = 1;
            break;
        }
    }
    XFree(prop_return);

    if (!exist) {
        XChangeProperty(wmDisplay, window, property, XA_ATOM, 32, PropModeAppend, (unsigned char*)&value, 1);
    }
}
static void removeAtom(Window window, Atom property, Atom value, unsigned maxAtomsCount, Atom* newAtoms) {
    Atom type_return;
    int format_return;
    unsigned long nitems;
    unsigned long bytes_after_return;
    unsigned long* prop_return;
    Status status = XGetWindowProperty(
            wmDisplay,
            window,
            _NET_WM_STATE,
            0,
            maxAtomsCount,
            False,
            AnyPropertyType,
            &type_return,
            &format_return,
            &nitems,
            &bytes_after_return,
            (unsigned char**)&prop_return
    );
    if (status != Success) {
        return;
    }

    int keepCount = 0;
    int i;
    for (i = 0; i < nitems; i++) {
        if (prop_return[i] != value) {
            newAtoms[keepCount] = prop_return[i];
            keepCount++;
        }
    }
    XFree(prop_return);

    if (nitems != keepCount) {
        XChangeProperty(
                wmDisplay,
                window,
                property,
                XA_ATOM,
                32,
                PropModeReplace,
                (unsigned char*)newAtoms,
                keepCount
        );
    }
}

static int estimateNewWindowSize(wmNode* node, wmNode* target, int *x, int *y, unsigned *width, unsigned *height) {
    if (node == target) {
        wmSplitMode orientation;
        float div;
        if (wmSplitOrientation != NONE || target->window) {
            orientation = target->window ? target->orientation : wmSplitOrientation;
            div = 2.0f;
        }
        else {
            orientation = node->orientation;
            div = target->numChildren + 1;
        }

        if (orientation == VERTICAL) {
            *height /= div;
            *y += *height * ((unsigned)div - 1);
        }
        else {
            *width /= div;
            *x += *width * ((unsigned)div - 1);
        }

        *x += gap;
        *y += gap;
        *width -= 2 * (gap + borderWidth);
        *height -= 2 * (gap + borderWidth);

        return 1;
    }
    else {
        wmNode* child;
        if (node->orientation == HORIZONTAL) {
            for (int i = 0; i < node->numChildren; i++) {
                child = node->nodes + i;
                unsigned w = *width;
                w = child->weight * *width;
                if (estimateNewWindowSize(child, target, x, y, &w, height)) {
                    *width = w;
                    return 1;
                }
                *x += w;
            }
        }
        else {
            for (int i = 0; i < node->numChildren; i++) {
                child = node->nodes + i;
                unsigned h = *height;
                h = child->weight * *height;
                if (estimateNewWindowSize(child, target, x, y, width, &h)) {
                    *height = h;
                    return 1;
                }
                *y += h;
            }
        }
    }

    return 0;
}

static void setActiveWindow(wmWorkspace* workspace, wmWindow* window) {
    if (window == NULL) {
        XDeleteProperty(wmDisplay, wmRoot, _NET_ACTIVE_WINDOW);
    }
    else {
        XChangeProperty(wmDisplay, wmRoot, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char*)&window->window, 1);

        int n;
        Atom* protocols;
        if (XGetWMProtocols(wmDisplay, window->window, &protocols, &n)) {
            while (n--) {
                if (protocols[n] == WM_TAKE_FOCUS) {
                    XEvent e;
                    e.type = ClientMessage;
                    e.xclient.window = window->window;
                    e.xclient.message_type = WM_PROTOCOLS;
                    e.xclient.format = 32;
                    e.xclient.data.l[0] = WM_TAKE_FOCUS;
                    e.xclient.data.l[1] = CurrentTime;
                    XSendEvent(wmDisplay, window->window, False, NoEventMask, &e);
                }
                break;
            }
            XFree(protocols);
        }
    }

    workspace->activeWindow = window;
}

static void stateHandler(XClientMessageEvent* event) {
    if (event->data.l[1] == _NET_WM_STATE_FULLSCREEN) {
        wmWindow* window = wmWindowTowmWindow(event->window);
        if (!window) {
            return;
        }

        if (!fullscreen) {
            XChangeProperty(wmDisplay, window->window, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char*)&_NET_WM_STATE_FULLSCREEN, 1);
            XUnmapWindow(wmDisplay, window->frame);
            XReparentWindow(wmDisplay, window->window, wmRoot, 0, 0);
            fullscreen = window;
            wmShowActiveWorkspace();
        }
        else {
            XChangeProperty(wmDisplay, window->window, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, NULL, 0);
            XWindowAttributes attributes;
            if (XGetWindowAttributes(wmDisplay, window->window, &attributes)) {
                XReparentWindow(wmDisplay, window->window, window->frame, 0, 0);
            }
            fullscreen = NULL;
            wmShowActiveWorkspace();
        }
    }
}
static void requestFrameExtents(XClientMessageEvent* event) {
    int x = gap;
    int y = gap;
    unsigned width = wmScreenWidth - 2 * gap;
    unsigned height = wmScreenHeight - 2 * gap;

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    estimateNewWindowSize(workspace->layout, workspace->splitNode, &x, &y, &width, &height);

    long bounds[] = { x, y, x + width, y + height };
    XChangeProperty(wmDisplay, event->window, _NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)bounds, 4);
}
static void netActiveWindow(XClientMessageEvent* event) {
    wmWindow* window = wmWindowTowmWindow(event->window);
    if (window) {
        setActiveWindow(&wmWorkspaces[wmActiveWorkspace], window);
        wmUpdateBorders();
    }
}


ClientMessageHandler clientMessageHandler[] = {
        { &_NET_WM_STATE,               stateHandler        },
        { &_NET_REQUEST_FRAME_EXTENTS,  requestFrameExtents },
        { &_NET_ACTIVE_WINDOW,          netActiveWindow     },
};
const unsigned clientMessageHandlersCount = LENGTH(clientMessageHandler);

static void updateNetNumberOfDesktops() {
    long count = 0;
    for  (int i = 0; i < WORKSPACE_COUNT; i++) {
        count += wmActiveWorkspace == i || wmWorkspaces[i].activeWindow != NULL;
    }

    XChangeProperty(wmDisplay, wmRoot, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&count, 1);
}

static void attachWindow(wmWindow* window) {
    window->next = wmHead;

    if (!wmHead) {
        wmTail = window;
    }
    if (wmHead) {
        wmHead->previous = window;
    }
    wmHead = window;
}
static void detachWindow(wmWindow* window) {
    if (!window->previous) {
        wmHead = window->next;
        if (wmHead) {
            wmHead->previous = NULL;
        }
    }
    else {
        window->previous->next = window->next;
    }

    if (!window->next) {
        wmTail = window->previous;
        if (wmTail) {
            wmTail->next = NULL;
        }
    }
    else {
        window->next->previous = window->previous;
    }
}

static void configureWindow(Window window, int x, int y, unsigned width, unsigned height) {
    XConfigureEvent event;
    event.type = ConfigureNotify;
    event.border_width = 0;
    event.x = x + borderWidth;
    event.y = y + borderWidth;
    event.width = width;
    event.height = height;
    event.display = wmDisplay;
    event.event = window;
    event.window = window;
    event.above = None;
    event.override_redirect = False;
    XSendEvent(wmDisplay, window, False, StructureNotifyMask, (XEvent*)&event);
    XSync(wmDisplay, False);
}
static void removeWindowFromLayout(wmWorkspace* workspace, wmWindow* window) {
    if (window->dialog) {
        return;
    }

    wmNode* windowNode = findNode(workspace->layout, window);
    wmNode* parent = removeNode(workspace->layout, windowNode);
    if (!parent) {
        free(workspace->layout);
        workspace->layout = NULL;
        wmUpdateBar();
    }

    workspace->splitNode = parent;
    workspace->showSplitBorder = 0;

    wmSplitOrientation = NONE;
}
static void addWindowToLayout(wmWorkspace* workspace, wmWindow* window) {
    if (window->dialog) {
        return;
    }

    wmNode** layout = &workspace->layout;
    if (!*layout) {
        *layout = calloc(1, sizeof(wmNode));
        (*layout)->window = window;
        (*layout)->weight = 1.0f;
        (*layout)->orientation = HORIZONTAL;
        (*layout)->x = 0;
        (*layout)->y = 0;
        (*layout)->width = wmScreenWidth;
        (*layout)->height = wmScreenHeight;
        workspace->splitNode = *layout;
        wmUpdateBar();
    }
    else {
        wmNode new = newNode(window);
        workspace->splitNode = addNode(workspace->splitNode, new, wmSplitOrientation);
    }

    wmSplitOrientation = NONE;
}
static void showNode(wmNode* node, int x, int y, unsigned width, unsigned height) {
    if (node->window) {
        x += gap;
        y += gap;
        height -= 2 * (gap + borderWidth);
        width -= 2 * (gap + borderWidth);
        XMoveResizeWindow(wmDisplay, node->window->frame, x, y, width, height);
        XResizeWindow(wmDisplay, node->window->window, width, height);

        int left = node->x;
        int right = node->x + node->width;
        int top = node->y;
        int bottom = node->y + node->height;
        int pointerWasInNode = 0;
        if (wmMouseX >= left && wmMouseX <= right && wmMouseY >= top && wmMouseY <= bottom) {
            pointerWasInNode = 1;
        }

        node->x = x;
        node->y = y;
        node->width = width;
        node->height = height;

        left = node->x;
        right = node->x + node->width;
        top = node->y;
        bottom = node->y + node->height;
        int pointerInNode = wmMouseX >= left && wmMouseX <= right && wmMouseY >= top && wmMouseY <= bottom;
        if (pointerWasInNode && !pointerInNode) {
            wmSkipNextEnterNotify = 1;
        }

        configureWindow(node->window->window, node->x, node->y, node->width, node->height);
    }
    else {
        node->x = x;
        node->y = y;
        node->width = width;
        node->height = height;

        wmNode* child;
        if (node->orientation == HORIZONTAL) {
            for (int i = 0; i < node->numChildren; i++) {
                child = node->nodes + i;
                int w = child->weight * width;
                showNode(child, x, y, w, height);
                x += w;
            }
        }
        else {
            for (int i = 0; i < node->numChildren; i++) {
                child = node->nodes + i;
                int h = child->weight * height;
                showNode(child, x, y, width, h);
                y += h;
            }
        }
    }
}

static wmWindow* visibleWindow(size_t offset, wmWindow* head, unsigned workspace) {
    wmWindow* activeWindow = wmWorkspaces[workspace].activeWindow;
    if (activeWindow) {
        wmWindow* focus = activeWindow;
        unsigned mask = 1 << workspace;
        while (1) {
            focus = *(wmWindow**)((char*)focus + offset);
            if (!focus) {
                focus = head;
            }
            if (!focus || focus == activeWindow) {
                return NULL;
            }
            if (focus->workspaces & mask) {
                return focus;
            }
        }
    }

    return NULL;
}

/*
 * Initialization, event loop, cleanup
 */
static int wmDetected = 0;
static int onWMDetected(Display* d, XErrorEvent* e) {
    wmDetected = 1;
    return 0;
}
static int errorHandler(Display* d, XErrorEvent* e) {
    logmsg("Error: %x", e->error_code);
    return 0;
}
static void initAtoms() {
    WM_PROTOCOLS                    = XInternAtom(wmDisplay, "WM_PROTOCOLS", False);
    WM_DELETE_WINDOW                = XInternAtom(wmDisplay, "WM_DELETE_WINDOW", False);
    WM_STATE                        = XInternAtom(wmDisplay, "WM_STATE", False);
    WM_TAKE_FOCUS                   = XInternAtom(wmDisplay, "WM_TAKE_FOCUS", False);
    _NET_SUPPORTED                  = XInternAtom(wmDisplay, "_NET_SUPPORTED", False);
    _NET_CLIENT_LIST                = XInternAtom(wmDisplay, "_NET_CLIENT_LIST", False);
    _NET_NUMBER_OF_DESKTOPS         = XInternAtom(wmDisplay, "_NET_NUMBER_OF_DESKTOPS", False);
    _NET_ACTIVE_WINDOW              = XInternAtom(wmDisplay, "_NET_ACTIVE_WINDOW", False);
    _NET_SUPPORTING_WM_CHECK        = XInternAtom(wmDisplay, "_NET_SUPPORTING_WM_CHECK", False);
    _NET_REQUEST_FRAME_EXTENTS      = XInternAtom(wmDisplay, "_NET_REQUEST_FRAME_EXTENTS", False);
    _NET_FRAME_EXTENTS              = XInternAtom(wmDisplay, "_NET_FRAME_EXTENTS", False);
    _NET_WM_NAME                    = XInternAtom(wmDisplay, "_NET_WM_NAME", False);
    _NET_WM_STATE                   = XInternAtom(wmDisplay, "_NET_WM_STATE", False);
    _NET_WM_STATE_HIDDEN            = XInternAtom(wmDisplay, "_NET_WM_STATE_HIDDEN", False);
    _NET_WM_STATE_FULLSCREEN        = XInternAtom(wmDisplay, "_NET_WM_STATE_FULLSCREEN", False);

    Atom supported[] = {
            _NET_SUPPORTED,
            _NET_CLIENT_LIST,
            _NET_NUMBER_OF_DESKTOPS,
            _NET_ACTIVE_WINDOW,
            _NET_SUPPORTING_WM_CHECK,
            _NET_REQUEST_FRAME_EXTENTS,
            _NET_FRAME_EXTENTS,
            _NET_WM_NAME,
            _NET_WM_STATE,
            _NET_WM_STATE_HIDDEN,
            _NET_WM_STATE_FULLSCREEN,
    };

    XChangeProperty(wmDisplay, wmRoot, _NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char*)supported, LENGTH(supported));
    XDeleteProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST);

    const char wmname[] = WINDOW_MANAGER_NAME;
    Atom utf8string = XInternAtom(wmDisplay, "UTF8_STRING", False);
    wmcheckwin = XCreateSimpleWindow(wmDisplay, wmRoot, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(wmDisplay, wmcheckwin, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(wmDisplay, wmcheckwin, _NET_WM_NAME, utf8string, 8, PropModeReplace, (unsigned char *) wmname, LENGTH(wmname));
    XChangeProperty(wmDisplay, wmRoot, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *) &wmcheckwin, 1);

    updateNetNumberOfDesktops();
}
static void freeWorkspaces() {
    for (int i = 0; i < LENGTH(wmWorkspaces); i++) {
        wmNode* layout = wmWorkspaces[i].layout;
        if (layout) {
            freeTree(layout);
            free(layout);
        }
    }
}
static void queryWindows() {
    XGrabServer(wmDisplay);
    Window root, parent;
    Window* windows;
    unsigned nwindows;
    if (XQueryTree(wmDisplay, wmRoot, &root, &parent, &windows, &nwindows)) {
        for (int i = 0; i < nwindows; i++) {
            XWindowAttributes attr;
            if (XGetWindowAttributes(wmDisplay, windows[i], &attr)) {
                if (attr.map_state == IsViewable) {
                    wmNewWindow(windows[i], &attr);
                }
            }
        }
    }
    XFree(windows);
    XUngrabServer(wmDisplay);
}
int wmInitialize() {
    if (!XInitThreads()) {
        return 0;
    }

    wmDisplay = XOpenDisplay(NULL);
    if (!wmDisplay) {
        return 0;
    }

    int screen = DefaultScreen(wmDisplay);
    wmRoot = RootWindow(wmDisplay, screen);
    wmScreenWidth = DisplayWidth(wmDisplay, screen);
    wmScreenHeight = DisplayHeight(wmDisplay, screen);

    XSetErrorHandler(onWMDetected);
    XSelectInput(wmDisplay, wmRoot, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(wmDisplay, False);
    if (wmDetected) {
        XCloseDisplay(wmDisplay);
        return 0;
    }

    system(startupScriptBath);

    XSetErrorHandler(errorHandler);

    initAtoms();

    // Visuals
    XVisualInfo *infos;
    XRenderPictFormat *fmt;
    int nitems;
    int i;

    XVisualInfo tpl = {
            .screen = screen,
            .depth = 32,
            .class = TrueColor
    };
    long masks = VisualScreenMask | VisualDepthMask | VisualClassMask;
    infos = XGetVisualInfo(wmDisplay, masks, &tpl, &nitems);
    wmVisual = NULL;
    for (i = 0; i < nitems; i++) {
        fmt = XRenderFindVisualFormat(wmDisplay, infos[i].visual);
        if (fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
            wmVisual = infos[i].visual;
            wmDepth = infos[i].depth;
            wmColormap = XCreateColormap(wmDisplay, wmRoot, wmVisual, AllocNone);
            break;
        }
    }
    XFree(infos);
    if (!wmVisual) {
        wmVisual = DefaultVisual(wmDisplay, screen);
        wmDepth = DefaultDepth(wmDisplay, screen);
        wmColormap = DefaultColormap(wmDisplay, screen);
    }

    wmCursor = XCreateFontCursor(wmDisplay, XC_arrow);
    XDefineCursor(wmDisplay, wmRoot, wmCursor);

    wmCreateBar();
    wmWindowAreaX = gap;
#ifdef bottomBar
    wmWindowAreaY = gap;
#else
    wmWindowAreaY = gap + wmBarHeight;
#endif
    wmWindowAreaWidth = wmScreenWidth - 2 * gap;
    wmWindowAreaHeight = wmScreenHeight - 2 * gap - wmBarHeight;

    initializeKeyBindings();
    queryWindows();
    wmShowActiveWorkspace();

    return 1;
}
void wmRun() {
    XEvent event;
    while (wmRunning) {
        XNextEvent(wmDisplay, &event);
        if (handler[event.type])
            handler[event.type](event);
    }
}
void wmFree() {
    wmDestroyBar();

    XDestroyWindow(wmDisplay, wmcheckwin);

    freeKeyBindings();
    freeWorkspaces();

    XGrabServer(wmDisplay);
    wmWindow* next;
    for (wmWindow* wmWindow = wmHead; wmWindow; wmWindow = next) {
        next = wmWindow->next;
        XReparentWindow(wmDisplay, wmWindow->window, wmRoot, 0, 0);
        XDestroyWindow(wmDisplay, wmWindow->frame);
        free(wmWindow);
    }
    XUngrabServer(wmDisplay);

    XFreeCursor(wmDisplay, wmCursor);

    XCloseDisplay(wmDisplay);
}

wmWindow* wmNextVisibleWindow(unsigned workspace) {
    return visibleWindow(offsetof(wmWindow, next), wmHead, workspace);
}
wmWindow* wmPreviousVisibleWindow(unsigned workspace) {
    return visibleWindow(offsetof(wmWindow, previous), wmTail, workspace);
}
void wmMoveActiveWindow(unsigned workspace) {
    if (workspace != wmActiveWorkspace) {
        wmWorkspace* source = &wmWorkspaces[wmActiveWorkspace];
        wmWorkspace* destination = &wmWorkspaces[workspace];
        if (source->activeWindow) {
            source->showSplitBorder = 0;
            destination->showSplitBorder = 0;

            removeWindowFromLayout(source, source->activeWindow);
            addWindowToLayout(destination, source->activeWindow);

            destination->activeWindow = source->activeWindow;
            destination->activeWindow->workspaces = 1U << workspace;
            setActiveWindow(source, wmNextVisibleWindow(wmActiveWorkspace));
            updateNetNumberOfDesktops();
            wmUpdateBorders();
            wmShowActiveWorkspace();
        }
    }
}
void wmToggleActiveWindow(unsigned workspaceIndex) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        unsigned mask = 1U << workspaceIndex;
        unsigned workspaces = activeWindow->workspaces ^ mask;
        if (!workspaces) {
            return;
        }

        activeWindow->workspaces = workspaces;
        wmWorkspace* workspace = &wmWorkspaces[workspaceIndex];
        if (workspaces & mask) {
            addWindowToLayout(workspace, activeWindow);
            workspace->activeWindow = activeWindow;
        }
        else {
            removeWindowFromLayout(workspace, activeWindow);
            setActiveWindow(workspace, wmNextVisibleWindow(workspaceIndex));
        }
        updateNetNumberOfDesktops();

        wmWorkspaces[wmActiveWorkspace].showSplitBorder = 0;
        wmUpdateBorders();

        if (workspaceIndex == wmActiveWorkspace) {
            wmShowActiveWorkspace();
        }
    }
}

void wmFocusWindow(wmWindow* window) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    setActiveWindow(workspace, window);
    workspace->showSplitBorder = 0;
    workspace->splitNode = findNode(workspace->layout, workspace->activeWindow);
    XSetInputFocus(wmDisplay, window->window, RevertToPointerRoot, CurrentTime);
    wmUpdateBorders();
}
void wmRequestCloseWindow(wmWindow* window) {
    Atom* supportedProtocols;
    int numSupportedProtocols;
    if (XGetWMProtocols(wmDisplay, window->window, &supportedProtocols, &numSupportedProtocols)) {
        for (int i = 0; i < numSupportedProtocols; i++) {
            if (supportedProtocols[i] == WM_DELETE_WINDOW) {
                XEvent message;
                memset(&message, 0, sizeof(XEvent));
                message.xclient.type = ClientMessage;
                message.xclient.message_type = WM_PROTOCOLS;
                message.xclient.window = window->window;
                message.xclient.format = 32;
                message.xclient.data.l[0] = WM_DELETE_WINDOW;
                XSendEvent(wmDisplay, window->window, False, 0, &message);
                break;
            }
        }

        XFree(supportedProtocols);
    }
    else {
        XKillClient(wmDisplay, window->window);
    }
}

/*
 * Window stack
 */
void wmNewWindow(Window window, const XWindowAttributes* attributes) {
    if (attributes->override_redirect) {
        return;
    }

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];

    XSetWindowAttributes frameAttr;
    frameAttr.colormap = wmColormap;
    frameAttr.border_pixel = borderColor;
    frameAttr.background_pixel = 0;

    Window frame = XCreateWindow(
            wmDisplay,
            wmRoot,
            attributes->x,
            attributes->y,
            attributes->width,
            attributes->height,
            borderWidth,
            wmDepth,
            CopyFromParent,
            wmVisual,
            CWColormap | CWBorderPixel | CWBackPixel,
            &frameAttr
    );

    XChangeProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*)&window, 1);

    XGrabButton(wmDisplay, Button1, 0, window, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);

    XSelectInput(wmDisplay, frame, SubstructureNotifyMask | SubstructureRedirectMask);
    XSelectInput(wmDisplay, window, EnterWindowMask | FocusChangeMask);
    XReparentWindow(wmDisplay, window, frame, 0, 0);
    XMapWindow(wmDisplay, window);

    wmWindow* new_wmWindow = calloc(1, sizeof(wmWindow));
    new_wmWindow->window = window;
    new_wmWindow->frame = frame;
    new_wmWindow->workspaces = 1 << wmActiveWorkspace;
    attachWindow(new_wmWindow);

    Window parent;
    if (XGetTransientForHint(wmDisplay, window, &parent)) {
        wmDialog* dialog = calloc(1, sizeof(wmDialog));
        dialog->window = new_wmWindow;
        dialog->width = MIN(attributes->width, wmScreenWidth);
        dialog->height = MIN(attributes->height, wmScreenHeight);
        new_wmWindow->dialog = dialog;

        dialog->x = wmScreenWidth / 2 - dialog->width / 2;
        dialog->y = wmScreenHeight / 2 - dialog->height / 2;
        unsigned width = dialog->width - 2 * borderWidth;
        unsigned height = dialog->height - 2 * borderWidth;

        XMoveResizeWindow(
                wmDisplay,
                frame,
                dialog->x,
                dialog->y,
                width,
                height
        );
        XResizeWindow(wmDisplay, window, width, height);

        configureWindow(window, dialog->x, dialog->y, width, height);

        dialog->next = wmDialogs;
        wmDialogs = dialog;
    }

    addWindowToLayout(workspace, new_wmWindow);
    setActiveWindow(workspace, new_wmWindow);
    updateNetNumberOfDesktops();
    workspace->showSplitBorder = 0;
    wmUpdateBorders();
}
void wmFreeWindow(wmWindow* window) {
    XUnmapWindow(wmDisplay, window->frame);
    XDestroyWindow(wmDisplay, window->frame);

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        unsigned mask = 1 << i;
        if (window->workspaces & mask) {
            wmWorkspace* workspace = &wmWorkspaces[i];
            wmWindow* activeWindow = workspace->activeWindow;
            if (activeWindow == window) {
                setActiveWindow(workspace, wmNextVisibleWindow(i));
            }
            removeWindowFromLayout(workspace, window);
        }
    }

    if (window->dialog) {
        wmDialog** next;
        for (next = &wmDialogs; *next != window->dialog; next = &(*next)->next);
        *next = (*next)->next;

        free(window->dialog);
    }

    detachWindow(window);
    free(window);

    XDeleteProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST);
    for (wmWindow* w = wmHead; w; w = w->next) {
        Window win = w->window;
        XChangeProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*)&win, 1);
    }

    wmUpdateBorders();
    wmShowActiveWorkspace();
}
wmWindow* wmWindowTowmWindow(Window window) {
    wmWindow* w;
    for (w = wmHead; w && window != w->window; w = w->next);
    return w;
}

void wmSelectWorkspace(unsigned workspaceIndex) {
    wmActiveWorkspace = workspaceIndex;
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    updateNetNumberOfDesktops();

    setActiveWindow(workspace, workspace->activeWindow);
    workspace->showSplitBorder = 0;
    wmUpdateBorders();
    wmShowActiveWorkspace();

    wmUpdateBar();
}
void wmShowActiveWorkspace() {
    int mask = 1 << wmActiveWorkspace;
    for (wmWindow* window = wmHead; window; window = window->next) {
        long data[2] = { None, None };
        if (window->workspaces & mask) {
            XMapWindow(wmDisplay, window->frame);
            data[0] = NormalState;

            Atom newAtoms[WM_STATE_SUPPORTED_COUNT - 1];
            removeAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, WM_STATE_SUPPORTED_COUNT, newAtoms);
        }
        else {
            XUnmapWindow(wmDisplay, window->frame);
            data[0] = IconicState;

            addAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, WM_STATE_SUPPORTED_COUNT);
        }

        XChangeProperty(wmDisplay, window->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
    }

    if (fullscreen) {
        XMoveResizeWindow(wmDisplay, fullscreen->window, 0, 0, wmScreenWidth, wmScreenHeight);
    }
    else {
        wmNode* layout = wmWorkspaces[wmActiveWorkspace].layout;
        if (!layout) {
            return;
        }

#ifdef smartGaps
        if (layout->window) {
            const int height = wmScreenHeight - wmBarHeight;
#ifdef bottomBar
            int y = 0;
#else
            int y = wmBarHeight;
#endif

            layout->x = 0;
            layout->y = y;
            layout->width = wmScreenWidth;
            layout->height = height;

            XMoveResizeWindow(wmDisplay, layout->window->frame, 0, y, wmScreenWidth, height);
            XResizeWindow(wmDisplay, layout->window->window, wmScreenWidth, height);
            configureWindow(layout->window->window, 0, y, wmScreenWidth, height);
        }
        else {
            Window root_return, child_return;
            int winx, winy;
            unsigned mask_return;
            XQueryPointer(wmDisplay, wmRoot, &root_return, &child_return, &wmMouseX, &wmMouseY, &winx, &winy, &mask_return);

            showNode(layout, wmWindowAreaX, wmWindowAreaY, wmWindowAreaWidth, wmWindowAreaHeight);
        }
#else
        Window root_return, child_return;
        int winx, winy;
        unsigned mask_return;
        XQueryPointer(wmDisplay, wmRoot, &root_return, &child_return, &wmMouseX, &wmMouseY, &winx, &winy, &mask_return);

        showNode(layout, wmWindowAreaX, wmWindowAreaY, wmWindowAreaWidth, wmWindowAreaHeight);
#endif
    }

    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        XSetInputFocus(wmDisplay, activeWindow->window, RevertToPointerRoot, CurrentTime);
    }
}

/*
 * Splitting
 */
void wmLowerSplit(wmSplitMode orientation) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    unsigned found = 0;
    for (int i = 0; i < workspace->splitNode->numChildren; i++) {
        wmNode* child = workspace->splitNode->nodes + i;
        if (findNode(child, workspace->activeWindow)) {
            workspace->splitNode = child;
            found = 1;
            break;
        }
    }

    workspace->showSplitBorder = found && (orientation != NONE || workspace->splitNode->numChildren >= 2);
    wmSplitOrientation = orientation;
    wmUpdateBorders();
}
void wmRaiseSplit(wmSplitMode orientation) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    if (workspace->showSplitBorder || orientation == NONE) {
        workspace->splitNode = findParent(workspace->layout, workspace->splitNode);
        if (!workspace->splitNode) {
            workspace->splitNode = workspace->layout;
        }
    }

    wmSplitOrientation = orientation;
    workspace->showSplitBorder = 1;
    wmUpdateBorders();
}

/*
 * Borders
 */
static void updateBorders(wmNode* node, int belowSplit) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (node->window) {
        unsigned color;
        if (workspace->showSplitBorder && belowSplit) {
            color = borderColorSplit;
        }
        else {
            color = node->window == workspace->activeWindow ? borderColorActive : borderColor;
        }
        XSetWindowBorder(wmDisplay, node->window->frame, color);

#ifdef smartGaps
        unsigned width;
        if (workspace->layout && workspace->layout->window) {
            width = 0;
        }
        else {
            width = borderWidth;
        }
        XSetWindowBorderWidth(wmDisplay, node->window->frame, width);
#endif

        return;
    }

    for (int i = 0; i < node->numChildren; i++) {
        wmNode* child = node->nodes + i;
        updateBorders(child, belowSplit || node == workspace->splitNode || child == workspace->splitNode);
    }
}
void wmUpdateBorders() {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    for (wmDialog* d = wmDialogs; d; d = d->next) {
        XSetWindowBorder(
                wmDisplay,
                d->window->frame,
                d->window == workspace->activeWindow ? borderColorActive : borderColor
        );
    }

    if (workspace->layout) {
        updateBorders(workspace->layout, workspace->layout == workspace->splitNode);
    }
}

/*
 * Resizing
 */
static int findLeftNode(wmNode* parent, wmWindow* window, wmNode** left, wmNode** right, unsigned width, unsigned* outWidth) {
    for (int i = 0; i < parent->numChildren; i++) {
        wmNode* child = parent->nodes + i;
        if (child->window == window || findLeftNode(child, window, left, right, parent->orientation == HORIZONTAL ? (child->weight * width) : width, outWidth)) {
            if (*left == NULL && *right == NULL && parent->orientation == HORIZONTAL && i != 0) {
                *left = &parent->nodes[i - 1];
                *right = child;
                *outWidth = width;
            }

            return 1;
        }
    }

    return 0;
}
static int findRightNode(wmNode* parent, wmWindow* window, wmNode** left, wmNode** right, unsigned width, unsigned* outWidth) {
    for (int i = 0; i < parent->numChildren; i++) {
        wmNode* child = parent->nodes + i;
        if (child->window == window || findRightNode(child, window, left, right, parent->orientation == HORIZONTAL ? (child->weight * width) : width, outWidth)) {
            if (*left == NULL && *right == NULL && parent->orientation == HORIZONTAL && (i + 1) < parent->numChildren) {
                *left = child;
                *right = &parent->nodes[i + 1];
                *outWidth = width;
            }

            return 1;
        }
    }

    return 0;
}
static int findUpperNode(wmNode* parent, wmWindow* window, wmNode** top, wmNode** bottom, unsigned height, unsigned* outHeight) {
    for (int i = 0; i < parent->numChildren; i++) {
        wmNode* child = parent->nodes + i;
        if (child->window == window || findUpperNode(child, window, top, bottom, parent->orientation == VERTICAL ? (child->weight * height) : height, outHeight)) {
            if (*top == NULL && *bottom == NULL && parent->orientation == VERTICAL && i != 0) {
                *top = &parent->nodes[i - 1];
                *bottom = child;
                *outHeight = height;
            }

            return 1;
        }
    }

    return 0;
}
static int findLowerNode(wmNode* parent, wmWindow* window, wmNode** top, wmNode** bottom, unsigned height, unsigned* outHeight) {
    for (int i = 0; i < parent->numChildren; i++) {
        wmNode* child = parent->nodes + i;
        if (child->window == window || findLowerNode(child, window, top, bottom, parent->orientation == VERTICAL ? (child->weight * height) : height, outHeight)) {
            if (*top == NULL && *bottom == NULL && parent->orientation == VERTICAL && (i + 1) < parent->numChildren) {
                *top = child;
                *bottom = &parent->nodes[i + 1];
                *outHeight = height;
            }

            return 1;
        }
    }

    return 0;
}
static void moveEdge(int (*finder)(wmNode*, wmWindow*, wmNode**, wmNode**, unsigned, unsigned*), unsigned direction, unsigned length, unsigned minLength) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    wmNode* a = NULL;
    wmNode* b = NULL;
    unsigned outLength;
    finder(workspace->layout, workspace->activeWindow, &a, &b, length, &outLength);
    if (a == b) {
        return;
    }

    wmNode* smaller;
    wmNode* bigger;

    float sum = a->weight + b->weight;
    if (direction) {
        smaller = a;
        bigger = b;
    }
    else {
        smaller = b;
        bigger = a;
    }

    smaller->weight -= resizeChange;
    bigger->weight += resizeChange;

    float minWeight = minLength / (float)outLength;
    if (smaller->weight < minWeight) {
        smaller->weight = minWeight;
        bigger->weight = sum - minWeight;
    }

    wmShowActiveWorkspace();
}
void wmMoveLeftEdgeHorizontally(wmHorizontalDirection direction) {
    moveEdge(findLeftNode, direction, wmScreenWidth, minWidth);
}
void wmMoveRightEdgeHorizontally(wmHorizontalDirection direction) {
    moveEdge(findRightNode, direction, wmScreenWidth, minWidth);
}
void wmMoveUpperEdgeVertically(wmVerticalDirection direction) {
    moveEdge(findUpperNode, direction, wmScreenHeight, minHeight);
}
void wmMoveLowerEdgeVertically(wmVerticalDirection direction) {
    moveEdge(findLowerNode, direction, wmScreenHeight, minHeight);
}

void wmConfigureWindow(wmWindow* window) {
    wmDialog* dialog = window->dialog;
    if (dialog) {
        configureWindow(window->window, dialog->x, dialog->y, dialog->width, dialog->height);
        return;
    }

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    wmNode* node = findNode(workspace->layout, window);
    if (node) {
        configureWindow(window->window, node->x, node->y, node->width, node->height);
    }
}

void wmMoveNode(wmMoveDirection direction) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    wmNode* layout = workspace->layout;
    if (!layout) {
        return;
    }

    int index;
    wmNode* parent = NULL;
    if (!indexOf(layout, workspace->splitNode, &parent, &index)) {
        return;
    }

    switch (direction) {
        case MOVE_LEFT:
            if (parent->orientation == HORIZONTAL && index >= 1) {
                swap(parent, index, index - 1);
                workspace->splitNode -= 1;
            }
            else {
                workspace->splitNode = raiseLeft(workspace->layout, parent, index);
            }
            wmShowActiveWorkspace();
            break;
        case MOVE_UP:
            if (parent->orientation == VERTICAL && index >= 1) {
                swap(parent, index, index - 1);
                workspace->splitNode -= 1;
            }
            else {
                workspace->splitNode = raiseUp(workspace->layout, parent, index);
            }
            wmShowActiveWorkspace();
            break;
        case MOVE_RIGHT:
            if (parent->orientation == HORIZONTAL && (index + 1) < parent->numChildren) {
                swap(parent, index, index + 1);
                workspace->splitNode += 1;
            }
            else {
                workspace->splitNode = raiseRight(workspace->layout, parent, index);
            }
            wmShowActiveWorkspace();
            break;
        case MOVE_DOWN:
            if (parent->orientation == VERTICAL && (index + 1) < parent->numChildren) {
                swap(parent, index, index + 1);
                workspace->splitNode += 1;
            }
            else {
                workspace->splitNode = raiseDown(workspace->layout, parent, index);
            }
            wmShowActiveWorkspace();
            break;
    }
}
