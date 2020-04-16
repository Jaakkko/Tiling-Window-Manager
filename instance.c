//
// Created by jaakko on 10.4.2020.
//

#include "instance.h"
#include "logger.h"
#include "input.h"
#include "events.h"
#include "util.h"
#include "config.h"

#include <stdlib.h>
#include <string.h>

#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

unsigned wmActiveWorkspace = 0;

wmWindow* wmHead = NULL;
wmWindow* wmTail = NULL;

wmWorkspace wmWorkspaces[WORKSPACE_COUNT];

int wmRunning = True;
int wmExitCode = 0;

static Window wmcheckwin;
static wmWindow* fullscreen = NULL;

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
            XResizeWindow(wmDisplay, window->window, wmScreenWidth, wmScreenHeight);
            XRaiseWindow(wmDisplay, window->window);
            fullscreen = window;
        }
        else {
            XChangeProperty(wmDisplay, window->window, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, NULL, 0);
            XWindowAttributes attributes;
            if (XGetWindowAttributes(wmDisplay, window->window, &attributes)) {
                int width = MIN(MAX(attributes.width, 1920), wmScreenWidth - 200);
                int height = MIN(MAX(attributes.height, 1080), wmScreenHeight - 200);
                int x = wmScreenWidth / 2 - width / 2;
                int y = wmScreenHeight / 2 - height / 2;
                XResizeWindow(wmDisplay, window->window, width, height);
                XMoveResizeWindow(wmDisplay, window->frame, x, y, width, height);
                XReparentWindow(wmDisplay, window->window, window->frame, 0, 0);
                XMapWindow(wmDisplay, window->frame);
                XRaiseWindow(wmDisplay, window->frame);
            }
            fullscreen = NULL;
        }
    }
}

ClientMessageHandler clientMessageHandler[] = {
        { &_NET_WM_STATE, stateHandler }
};
const unsigned clientMessageHandlersCount = LENGTH(clientMessageHandler);

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

static void freeNodesRecursive(wmNode* node) {
    for (int i = 0; i < node->numChildren; i++) {
        freeNodesRecursive(node->nodes + i);
    }

    free(node->nodes);
    node->nodes = NULL;
    node->numChildren = 0;
}
static int removeWindowFromNode(wmNode* node, wmWindow* window) {
    wmNode* child;
    for (int i = 0; i < node->numChildren; i++) {
        child = node->nodes + i;
        if (child->window == window) {
            node->numChildren--;
            if (node->numChildren == 1) {
                node->window = node->nodes[1 ^ i].window;
                free(node->nodes);
                node->nodes = NULL;
                node->numChildren = 0;
            }
            else {
                memcpy(child, child + 1, (node->numChildren - i) * sizeof(wmNode));
                node->nodes = realloc(node->nodes, node->numChildren * sizeof(wmNode));
            }
            return 1;
        }

        if (removeWindowFromNode(child, window)) {
            return 1;
        }
    }

    return 0;
}
static void addWindowToNode(wmNode* node, wmWindow* window) {
    if (node->nodes) {
        node->numChildren++;
        node->nodes = realloc(node->nodes, node->numChildren * sizeof(wmNode));

        wmNode newNode = { 0, NULL, window };
        node->nodes[node->numChildren - 1] = newNode;
    }
    else if (node->window) {
        node->nodes = calloc(2, sizeof(wmNode));
        node->nodes[0].window = node->window;
        node->nodes[1].window = window;
        node->numChildren = 2;
        node->window = NULL;
    }
    else {
        logmsg("addWindowToNode ERROR");
    }
}
static void removeWindowFromLayout(wmNode** layout, wmWindow* window) {
    if (!removeWindowFromNode(*layout, window)) {
        free(*layout);
        *layout = NULL;
    }
}
static void addWindowToLayout(wmNode** layout, wmWindow* window) {
    if (!*layout) {
        *layout = calloc(1, sizeof(wmNode));
        (*layout)->window = window;
    }
    else {
        addWindowToNode(*layout, window);
    }
}
static void showNode(wmNode* node, int x, int y, unsigned width, unsigned height) {
    if (node->window) {
        unsigned ww = width - 2 * borderWidth;
        unsigned wh = height - 2 * borderWidth;
        XMoveResizeWindow(wmDisplay, node->window->frame, x, y, ww, wh);
        XResizeWindow(wmDisplay, node->window->window, ww, wh);
        XMapWindow(wmDisplay, node->window->frame);
    }
    else {
        width = width / node->numChildren;
        wmNode* child;
        for (int i = 0; i < node->numChildren; i++) {
            child = node->nodes + i;
            showNode(child, x, y, width, height);
            x += (int)width;
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
    _NET_SUPPORTED                  = XInternAtom(wmDisplay, "_NET_SUPPORTED", False);
    _NET_CLIENT_LIST                = XInternAtom(wmDisplay, "_NET_CLIENT_LIST", False);
    _NET_SUPPORTING_WM_CHECK        = XInternAtom(wmDisplay, "_NET_SUPPORTING_WM_CHECK", False);
    _NET_WM_NAME                    = XInternAtom(wmDisplay, "_NET_WM_NAME", False);
    _NET_WM_STATE                   = XInternAtom(wmDisplay, "_NET_WM_STATE", False);
    _NET_WM_STATE_FULLSCREEN        = XInternAtom(wmDisplay, "_NET_WM_STATE_FULLSCREEN", False);

    Atom supported[] = {
            _NET_SUPPORTED,
            _NET_CLIENT_LIST,
            _NET_SUPPORTING_WM_CHECK,
            _NET_WM_NAME,
            _NET_WM_STATE,
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
}
static void freeWorkspaces() {
    for (int i = 0; i < LENGTH(wmWorkspaces); i++) {
        wmNode* layout = wmWorkspaces[i].layout;
        if (layout) {
            freeNodesRecursive(layout);
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
            removeWindowFromLayout(&source->layout, source->activeWindow);
            addWindowToLayout(&destination->layout, source->activeWindow);

            destination->activeWindow = source->activeWindow;
            destination->activeWindow->workspaces = 1U << workspace;
            source->activeWindow = wmNextVisibleWindow(wmActiveWorkspace);
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
        wmNode** layout = &workspace->layout;
        if (workspaces & mask) {
            addWindowToLayout(layout, activeWindow);
            workspace->activeWindow = activeWindow;
        }
        else {
            removeWindowFromLayout(layout, activeWindow);
            workspace->activeWindow = wmNextVisibleWindow(workspaceIndex);
        }

        if (workspaceIndex == wmActiveWorkspace) {
            wmShowActiveWorkspace();
        }
    }
}

void wmFocusWindow(wmWindow* window) {
    wmWorkspaces[wmActiveWorkspace].activeWindow = window;
    XSetInputFocus(wmDisplay, window->window, RevertToPointerRoot, CurrentTime);
}
void wmRequestCloseWindow(wmWindow* window) {
    Atom* supportedProtocols;
    int numSupportedProtocols;
    if (XGetWMProtocols(wmDisplay, window->window, &supportedProtocols, &numSupportedProtocols)) {
        for (int i = 0; i < numSupportedProtocols; i++) {
            if (supportedProtocols[i] == WM_DELETE_WINDOW) {
                XEvent message;
                message.xclient.type = ClientMessage;
                message.xclient.message_type = WM_PROTOCOLS;
                message.xclient.window = window->window;
                message.xclient.format = 32;
                message.xclient.data.l[0] = WM_DELETE_WINDOW;
                XSendEvent(wmDisplay, window->window, False, 0, &message);
            }
        }
    }
    else {
        XKillClient(wmDisplay, window->window);
    }
}

void wmNewWindow(Window window, const XWindowAttributes* attributes) {
    if (attributes->override_redirect) {
        return;
    }

    XSetWindowAttributes frameAttr;
    frameAttr.colormap = wmColormap;
    frameAttr.border_pixel = 0x80789F43;
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

    XSelectInput(wmDisplay, frame, SubstructureNotifyMask | SubstructureRedirectMask);
    XReparentWindow(wmDisplay, window, frame, 0, 0);
    XMapWindow(wmDisplay, window);
    XMoveWindow(wmDisplay, frame, attributes->x, attributes->y);

    wmWindow* new_wmWindow = calloc(1, sizeof(wmWindow));
    new_wmWindow->window = window;
    new_wmWindow->frame = frame;
    new_wmWindow->workspaces = 1 << wmActiveWorkspace;
    attachWindow(new_wmWindow);

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    addWindowToLayout(&workspace->layout, new_wmWindow);
    workspace->activeWindow = new_wmWindow;
}
void wmFreeWindow(wmWindow* window) {
    XUnmapWindow(wmDisplay, window->frame);
    XDestroyWindow(wmDisplay, window->frame);

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        unsigned mask = 1 << i;
        if (window->workspaces & mask) {
            wmWindow* activeWindow = wmWorkspaces[i].activeWindow;
            if (activeWindow) {
                while (1) {
                    activeWindow = activeWindow->next ? activeWindow->next : wmHead;
                    if (activeWindow == wmWorkspaces[i].activeWindow) {
                        wmWorkspaces[i].activeWindow = NULL;
                        break;
                    }
                    if (activeWindow->workspaces & mask) {
                        wmWorkspaces[i].activeWindow = activeWindow;
                        break;
                    }
                }
            }

            removeWindowFromLayout(&wmWorkspaces[i].layout, window);
        }
    }
    detachWindow(window);
    free(window);

    XDeleteProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST);
    for (wmWindow* w = wmHead; w; w = w->next) {
        Window win = w->window;
        XChangeProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char*)&win, 1);
    }

    wmShowActiveWorkspace();
}
wmWindow* wmWindowTowmWindow(Window window) {
    wmWindow* w;
    for (w = wmHead; w && window != w->window; w = w->next);
    return w;
}

void wmShowActiveWorkspace() {
    for (wmWindow* window = wmHead; window; window = window->next) {
        XUnmapWindow(wmDisplay, window->frame);
    }

    wmNode* layout = wmWorkspaces[wmActiveWorkspace].layout;
    if (!layout) {
        return;
    }

    showNode(layout, 0, 0, wmScreenWidth, wmScreenHeight);

    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        XSetInputFocus(wmDisplay, activeWindow->window, RevertToPointerRoot, CurrentTime);
    }
}
