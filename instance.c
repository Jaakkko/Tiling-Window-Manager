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

wmSplitMode wmSplitOrientation = HORIZONTAL;

wmWorkspace wmWorkspaces[WORKSPACE_COUNT];

int wmRunning = True;
int wmExitCode = 0;

static Window wmcheckwin;
static wmWindow* fullscreen = NULL;

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

ClientMessageHandler clientMessageHandler[] = {
        { &_NET_WM_STATE,               stateHandler        },
        { &_NET_REQUEST_FRAME_EXTENTS,  requestFrameExtents },
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
        return;
    }

    for (int i = 0; i < node->numChildren; i++) {
        wmNode* child = node->nodes + i;
        updateBorders(child, belowSplit || node == workspace->splitNode || child == workspace->splitNode);
    }
}

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

static wmNode* findNode(wmNode* node, wmWindow* window) {
    if (node->window == window) {
        return node;
    }

    for (int i = 0; i < node->numChildren; i++) {
        wmNode* found = findNode(node->nodes + i, window);
        if (found) {
            return found;
        }
    }

    return NULL;
}
static wmNode* findParent(wmNode* node, wmNode* child) {
    for (int i = 0; i < node->numChildren; i++) {
        wmNode* ptr = node->nodes + i;
        if (ptr == child) {
            return node;
        }

        wmNode* found = findParent(ptr, child);
        if (found) {
            return found;
        }
    }

    return NULL;
}
static void freeNodesRecursive(wmNode* node) {
    for (int i = 0; i < node->numChildren; i++) {
        freeNodesRecursive(node->nodes + i);
    }

    free(node->nodes);
    node->nodes = NULL;
    node->numChildren = 0;
}
static wmNode* removeWindowFromNode(wmNode* node, wmWindow* window) {
    wmNode* child;
    for (int i = 0; i < node->numChildren; i++) {
        child = node->nodes + i;
        if (child->window == window) {
            node->numChildren--;
            if (node->numChildren == 1) {
                wmNode lastOne = node->nodes[1 ^ i];
                lastOne.weight = node->weight;
                free(node->nodes);
                memcpy(node, &lastOne, sizeof(wmNode));
            }
            else {
                memcpy(child, child + 1, (node->numChildren - i) * sizeof(wmNode));
                node->nodes = realloc(node->nodes, node->numChildren * sizeof(wmNode));

                float weight = 1.0f / node->numChildren;
                for (int j = 0; j < node->numChildren; j++) {
                    node->nodes[j].weight = weight;
                }
            }

            return node;
        }

        wmNode* found = removeWindowFromNode(child, window);
        if (found) {
            return found;
        }
    }

    return NULL;
}
static wmNode* addWindowToNode(wmNode* node, wmWindow* window, unsigned split) {
    if (split || node->window) {
        wmNode oldNode = *node;
        node->orientation = wmSplitOrientation == NONE ? node->orientation : wmSplitOrientation;
        node->nodes = calloc(2, sizeof(wmNode));
        node->window = NULL;
        node->numChildren = 2;

        node->nodes[0] = oldNode;
        node->nodes[1].window = window;
        node->nodes[0].weight = 0.5f;
        node->nodes[1].weight = 0.5f;

        wmSplitOrientation = !wmSplitOrientation;
        return &node->nodes[1];
    }

    if (node->nodes) {
        node->numChildren++;
        node->nodes = realloc(node->nodes, node->numChildren * sizeof(wmNode));

        int index = node->numChildren - 1;
        wmNode newNode = { 0, NULL, window };
        node->nodes[index] = newNode;

        float weight = 1.0f / node->numChildren;
        for (int i = 0; i < node->numChildren; i++) {
            node->nodes[i].weight = weight;
        }

        return &node->nodes[index];
    }
    else {
        logmsg("addWindowToNode ERROR");
    }

    return NULL;
}
static void removeWindowFromLayout(wmWorkspace* workspace, wmWindow* window) {
    wmNode* parent = removeWindowFromNode(workspace->layout, window);
    if (!parent) {
        free(workspace->layout);
        workspace->layout = NULL;
    }
    else if (workspace->layout->window) {
        wmSplitOrientation = HORIZONTAL;
    }

    workspace->splitNode = parent;
    workspace->showSplitBorder = 0;
}
static void addWindowToLayout(wmWorkspace* workspace, wmWindow* window) {
    wmNode** layout = &workspace->layout;
    if (!*layout) {
        *layout = calloc(1, sizeof(wmNode));
        (*layout)->window = window;
        (*layout)->weight = 1.0f;
        (*layout)->orientation = HORIZONTAL;
        workspace->splitNode = *layout;
    }
    else {
        workspace->splitNode = addWindowToNode(workspace->splitNode, window, wmSplitOrientation != NONE);
    }
}
static void showNode(wmNode* node, int x, int y, unsigned width, unsigned height) {
    if (node->window) {
        x += gap;
        y += gap;
        height -= 2 * (gap + borderWidth);
        width -= 2 * (gap + borderWidth);
        XMoveResizeWindow(wmDisplay, node->window->frame, x, y, width, height);
        XResizeWindow(wmDisplay, node->window->window, width, height);

        XConfigureEvent event;
        event.type = ConfigureNotify;
        event.border_width = 0;
        event.x = x + borderWidth;
        event.y = y + borderWidth;
        event.width = width;
        event.height = height;
        event.display = wmDisplay;
        event.event = node->window->window;
        event.window = node->window->window;
        event.above = None;
        event.override_redirect = False;
        XSendEvent(wmDisplay, node->window->window, False, StructureNotifyMask, (XEvent*)&event);
    }
    else {
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
    _NET_SUPPORTED                  = XInternAtom(wmDisplay, "_NET_SUPPORTED", False);
    _NET_CLIENT_LIST                = XInternAtom(wmDisplay, "_NET_CLIENT_LIST", False);
    _NET_SUPPORTING_WM_CHECK        = XInternAtom(wmDisplay, "_NET_SUPPORTING_WM_CHECK", False);
    _NET_REQUEST_FRAME_EXTENTS      = XInternAtom(wmDisplay, "_NET_REQUEST_FRAME_EXTENTS", False);
    _NET_FRAME_EXTENTS              = XInternAtom(wmDisplay, "_NET_FRAME_EXTENTS", False);
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
            source->showSplitBorder = 0;
            destination->showSplitBorder = 0;

            removeWindowFromLayout(source, source->activeWindow);
            addWindowToLayout(destination, source->activeWindow);

            destination->activeWindow = source->activeWindow;
            destination->activeWindow->workspaces = 1U << workspace;
            source->activeWindow = wmNextVisibleWindow(wmActiveWorkspace);
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
            workspace->activeWindow = wmNextVisibleWindow(workspaceIndex);
        }

        wmWorkspaces[wmActiveWorkspace].showSplitBorder = 0;
        wmUpdateBorders();

        if (workspaceIndex == wmActiveWorkspace) {
            wmShowActiveWorkspace();
        }
    }
}

void wmFocusWindow(wmWindow* window) {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    workspace->activeWindow = window;
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

    XSelectInput(wmDisplay, frame, SubstructureNotifyMask | SubstructureRedirectMask);
    XReparentWindow(wmDisplay, window, frame, 0, 0);
    XMapWindow(wmDisplay, window);

    wmWindow* new_wmWindow = calloc(1, sizeof(wmWindow));
    new_wmWindow->window = window;
    new_wmWindow->frame = frame;
    new_wmWindow->workspaces = 1 << wmActiveWorkspace;
    attachWindow(new_wmWindow);

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    addWindowToLayout(workspace, new_wmWindow);
    workspace->activeWindow = new_wmWindow;
    workspace->showSplitBorder = 0;
    wmUpdateBorders();
}
void wmFreeWindow(wmWindow* window) {
    XUnmapWindow(wmDisplay, window->frame);
    XDestroyWindow(wmDisplay, window->frame);

    for (int i = 0; i < WORKSPACE_COUNT; i++) {
        unsigned mask = 1 << i;
        if (window->workspaces & mask) {
            wmWindow** activeWindow = &wmWorkspaces[i].activeWindow;
            if (*activeWindow == window) {
                *activeWindow = wmNextVisibleWindow(i);
            }

            removeWindowFromLayout(&wmWorkspaces[i], window);
        }
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
    if (!workspace->layout || workspace->layout->window) {
        wmSplitOrientation = HORIZONTAL;
    }

    workspace->showSplitBorder = 0;
    wmUpdateBorders();
    wmShowActiveWorkspace();
}
void wmShowActiveWorkspace() {
    int mask = 1 << wmActiveWorkspace;
    for (wmWindow* window = wmHead; window; window = window->next) {
        long data[2] = { None, None };
        if (window->workspaces & mask) {
            XMapWindow(wmDisplay, window->frame);
            data[0] = NormalState;
        }
        else {
            XUnmapWindow(wmDisplay, window->frame);
            data[0] = WithdrawnState;
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

        showNode(layout, gap, gap, wmScreenWidth - 2 * gap, wmScreenHeight - 2 * gap);
    }

    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        XSetInputFocus(wmDisplay, activeWindow->window, RevertToPointerRoot, CurrentTime);
    }
}

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
void wmUpdateBorders() {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (workspace->layout) {
        updateBorders(workspace->layout, workspace->layout == workspace->splitNode);
    }
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
