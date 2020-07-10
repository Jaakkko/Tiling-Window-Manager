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
#include <stdio.h>
#include <string.h>

#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrender.h>

int wmMouseX;
int wmMouseY;

unsigned wmActiveWorkspace = 0;

wmWorkspace wmWorkspaces[WORKSPACE_COUNT];

int wmRunning = True;
int wmExitCode = 0;

Cursor wmCursors[CURSOR_LAST];

Display* wmDisplay;
Window wmRoot;
int wmDepth;
Visual* wmVisual;
Colormap wmColormap;
int wmScreenWidth;
int wmScreenHeight;
int wmWindowAreaX;
int wmWindowAreaY;
int wmWindowAreaWidth;
int wmWindowAreaHeight;

int skipNextEnterNotify = 0;

static wmFloatingWindow* wmFloatingWindows = NULL;

static wmWindow* wmHead = NULL;
static wmWindow* wmTail = NULL;

static wmSplitMode wmSplitOrientation = NONE;

static Window wmcheckwin;

static Atom
        WM_PROTOCOLS,
        WM_DELETE_WINDOW,
        WM_STATE,
        WM_TAKE_FOCUS,
        UTF8_STRING,
        _NET_SUPPORTED,
        _NET_CLIENT_LIST,
        _NET_NUMBER_OF_DESKTOPS,
        _NET_DESKTOP_GEOMETRY,
        _NET_DESKTOP_VIEWPORT,
        _NET_CURRENT_DESKTOP,
        _NET_DESKTOP_NAMES,
        _NET_ACTIVE_WINDOW,
        _NET_SUPPORTING_WM_CHECK,
        _NET_REQUEST_FRAME_EXTENTS,
        _NET_FRAME_EXTENTS,
        _NET_WM_NAME,
        _NET_WM_WINDOW_TYPE,
        _NET_WM_WINDOW_TYPE_DIALOG,
        _NET_WM_STATE,
        _NET_WM_STATE_STICKY,
        _NET_WM_STATE_HIDDEN,
        _NET_WM_STATE_FULLSCREEN,
        _NET_WM_ALLOWED_ACTIONS,
        _NET_WM_ACTION_FULLSCREEN;

static int getAction(Window window, long* data, Atom atom) {
    int add;
    switch (data[0]) {
        case _NET_WM_STATE_REMOVE:
            add = 0;
            break;
        case _NET_WM_STATE_ADD:
            add = 1;
            break;
        case _NET_WM_STATE_TOGGLE: {
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
                    _NET_WM_STATE_SUPPORTED_COUNT,
                    False,
                    AnyPropertyType,
                    &type_return,
                    &format_return,
                    &nitems,
                    &bytes_after_return,
                    (unsigned char**)&prop_return
            );

            if (status != Success) {
                add = 1;
                break;
            }

            int found = 0;
            for (int j = 0; j < nitems; j++) {
                if (prop_return[j] == atom) {
                    found = 1;
                    break;
                }
            }
            add = !found;
            XFree(prop_return);
            break;
        }
    }
    return add;
}
static int containsAtomValue(Window window, Atom property, Atom value, unsigned maxAtomsCount) {
    Atom type_return;
    int format_return;
    unsigned long nitems;
    unsigned long bytes_after_return;
    unsigned long* prop_return;
    Status status = XGetWindowProperty(
            wmDisplay,
            window,
            property,
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
        return 0;
    }

    int found = 0;
    for (int i = 0; i < nitems; i++) {
        if (prop_return[i] == value) {
            found = 1;
            break;
        }
    }
    XFree(prop_return);
    return found;
}
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
        if (keepCount == 0) {
            XDeleteProperty(wmDisplay, window, property);
        }
        else {
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

static void updateWorkspaceAtoms() {
    char desktopNames[2 * WORKSPACE_COUNT];
    long count = 0;
    for  (int i = 0; i < WORKSPACE_COUNT; i++) {
        if (wmActiveWorkspace == i || wmWorkspaces[i].activeWindow != NULL) {
            sprintf(desktopNames + 2 * count, "%i", i + 1);
            count++;
        }
    }

    XChangeProperty(wmDisplay, wmRoot, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&count, 1);
    XChangeProperty(wmDisplay, wmRoot, _NET_DESKTOP_NAMES, UTF8_STRING, 8, PropModeReplace, (unsigned char*)desktopNames, 2 * count);
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

static void updateNetFrameExtens(Window window) {
#ifdef smartGaps
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    long bw = (!workspace->layout || workspace->layout->window) ? 0 : borderWidth;
    long bounds[] = { bw, bw, bw, bw };
#else
    long bounds[] = { borderWidth, borderWidth, borderWidth, borderWidth };
#endif
    XChangeProperty(wmDisplay, window, _NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)bounds, 4);
}

static void configureWindow(Window window, int x, int y, unsigned width, unsigned height, int windowBorderWidth) {
    XConfigureEvent event;
    event.type = ConfigureNotify;
    event.border_width = 0;
    event.x = x + windowBorderWidth;
    event.y = y + windowBorderWidth;
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
    if (window->floating) {
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
    if (window->floating) {
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

        node->x = x;
        node->y = y;
        node->width = width;
        node->height = height;

        XWindowChanges wc;
        wc.x = 0;
        wc.y = 0;
        wc.width = width;
        wc.height = height;
        XConfigureWindow(wmDisplay, node->window->window, CWX | CWY | CWWidth | CWHeight, &wc);
        XMoveResizeWindow(wmDisplay, node->window->frame, x, y, width, height);
        configureWindow(node->window->window, x, y, node->width, node->height, borderWidth);
        updateNetFrameExtens(node->window->window);
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

static void setFloatingWindow(wmWindow* window, const XWindowAttributes* attributes, int windowBorderWidth) {
    wmFloatingWindow* floating = calloc(1, sizeof(wmFloatingWindow));
    floating->window = window;
    floating->width = MIN(attributes->width, wmScreenWidth);
    floating->height = MIN(attributes->height, wmScreenHeight);
    floating->x = attributes->x - windowBorderWidth;
    floating->y = attributes->y - windowBorderWidth;
    window->floating = floating;

    XSetWindowBorderWidth(wmDisplay, window->frame, windowBorderWidth);
    XMoveResizeWindow(
            wmDisplay,
            window->frame,
            floating->x,
            floating->y,
            floating->width,
            floating->height
    );
    XResizeWindow(wmDisplay, window->window, floating->width, floating->height);

    configureWindow(window->window, floating->x, floating->y, floating->width, floating->height, windowBorderWidth);

    floating->next = wmFloatingWindows;
    wmFloatingWindows = floating;
}
static void unsetFloatingWindow(wmWindow* window) {
    wmFloatingWindow** next;
    for (next = &wmFloatingWindows; *next != window->floating; next = &(*next)->next);
    *next = (*next)->next;

    free(window->floating);
    window->floating = NULL;
}

static void hideFullscreenWindow(wmWorkspace* workspace) {
    long data[2] = { None, None };
    XUnmapWindow(wmDisplay, workspace->fullscreen->frame);
    data[0] = IconicState;
    addAtom(workspace->fullscreen->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT);
    XChangeProperty(wmDisplay, workspace->fullscreen->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
}
static void showFullscreenWindow(wmWorkspace* workspace) {
    long data[2] = { None, None };
    XMapWindow(wmDisplay, workspace->fullscreen->frame);
    data[0] = NormalState;
    Atom newAtoms[_NET_WM_STATE_SUPPORTED_COUNT];
    removeAtom(workspace->fullscreen->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT, newAtoms);
    XChangeProperty(wmDisplay, workspace->fullscreen->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
}
static void unsetFullscreen(wmWindow* window) {
    Atom newAtoms[_NET_WM_STATE_SUPPORTED_COUNT];
    removeAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_SUPPORTED_COUNT, newAtoms);
    for (int j = 0; j < WORKSPACE_COUNT; j++) {
        wmWorkspace* workspace = &wmWorkspaces[j];
        if (workspace->fullscreen == window) {
            workspace->fullscreen = NULL;
        }
    }
    window->fullscreen = 0;
}
static void setFullscreen(wmWindow* window) {
    addAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_FULLSCREEN, _NET_WM_STATE_SUPPORTED_COUNT);
    for (int j = 0; j < WORKSPACE_COUNT; j++) {
        if (window->workspaces & (1 << j)) {
            wmWorkspace* workspace = &wmWorkspaces[j];
            if (workspace->fullscreen) {
                unsetFullscreen(workspace->fullscreen);
            }
            workspace->fullscreen = window;
        }
    }
    window->fullscreen = 1;
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
            if (focus->workspaces & mask || focus->floating && focus->floating->flags & FLOATING_STICKY) {
                return focus;
            }
        }
    }

    return NULL;
}

static void stateHandler(XClientMessageEvent* event) {
    for (int i = 0; i < 2; i++) {
        Atom property = event->data.l[i + 1];
        if (!property) {
            break;
        }

        if (property == _NET_WM_STATE_FULLSCREEN) {
            wmWindow* window = wmWindowTowmWindow(event->window);
            if (!window) {
                return;
            }
            int enable = getAction(window->window, event->data.l, _NET_WM_STATE_FULLSCREEN);
            if (enable) {
                if (!window->fullscreen) {
                    setFullscreen(window);
                    wmUpdateBorders();
                    wmShowActiveWorkspace();
                }
            }
            else if (window->fullscreen) {
                unsetFullscreen(window);
                wmUpdateBorders();
                wmShowActiveWorkspace();
            }
        }
        else if (property == _NET_WM_STATE_STICKY) {
            wmWindow* window = wmWindowTowmWindow(event->window);
            if (!window) {
                return;
            }

            int enable = getAction(window->window, event->data.l, _NET_WM_STATE_STICKY);
            if (enable) {
                if (!window->floating) {
                    for (int j = 0; j < WORKSPACE_COUNT; j++) {
                        if (window->workspaces & (1 << j)) {
                            removeWindowFromLayout(&wmWorkspaces[j], window);
                        }
                    }

                    XWindowAttributes attrs;
                    XGetWindowAttributes(wmDisplay, window->window, &attrs);
                    setFloatingWindow(window, &attrs, borderWidth);
                    window->floating->flags |= FLOATING_STICKY;
                }
                else if (window->floating->flags & FLOATING_STICKY) {
                    continue;
                }

                XChangeProperty(wmDisplay, window->window, _NET_WM_STATE, XA_ATOM, 32, PropModeAppend, (unsigned char*)&_NET_WM_STATE_STICKY, 1);
                wmShowActiveWorkspace();
            }
            else if (window->floating && window->floating->flags & FLOATING_STICKY) {
                Atom newAtoms[_NET_WM_STATE_SUPPORTED_COUNT];
                removeAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_STICKY, _NET_WM_STATE_SUPPORTED_COUNT, newAtoms);

                window->floating->flags ^= FLOATING_STICKY;
                if (!window->floating->flags) {
                    unsetFloatingWindow(window);
                }

                for (int j = 0; j < WORKSPACE_COUNT; j++) {
                    wmWorkspace* workspace = &wmWorkspaces[j];
                    if (window->workspaces & (1 << j)) {
                        addWindowToLayout(&wmWorkspaces[j], window);
                    }
                    else if (workspace->activeWindow == window) {
                        workspace->activeWindow = wmNextVisibleWindow(j);
                    }
                }

                wmShowActiveWorkspace();
            }
        }
    }
}
static void requestFrameExtents(XClientMessageEvent* event) {
    updateNetFrameExtens(event->window);
}
static void netActiveWindow(XClientMessageEvent* event) {
    wmWindow* window = wmWindowTowmWindow(event->window);
    if (window) {
        setActiveWindow(&wmWorkspaces[wmActiveWorkspace], window);
        wmUpdateBorders();
    }
}
static void netCurrentDesktop(XClientMessageEvent* event) {
    wmSelectWorkspace(event->data.l[0]);
}

ClientMessageHandler clientMessageHandler[] = {
        { &_NET_WM_STATE,               stateHandler        },
        { &_NET_REQUEST_FRAME_EXTENTS,  requestFrameExtents },
        { &_NET_ACTIVE_WINDOW,          netActiveWindow     },
        { &_NET_CURRENT_DESKTOP,        netCurrentDesktop   },
};
const unsigned clientMessageHandlersCount = LENGTH(clientMessageHandler);

/*
 * Initialization, event loop, cleanup
 */
static int wmDetected = 0;
static int onWMDetected(Display* d, XErrorEvent* e) {
    wmDetected = 1;
    return 0;
}
static int errorHandler(Display* d, XErrorEvent* e) {
#ifdef LOG_ERRORS
    if (e->request_code == X_CreateWindow) logmsg("X_CreateWindow");
    else if (e->request_code == X_ChangeWindowAttributes) logmsg("X_ChangeWindowAttributes");
    else if (e->request_code == X_GetWindowAttributes) logmsg("X_GetWindowAttributes");
    else if (e->request_code == X_DestroyWindow) logmsg("X_DestroyWindow");
    else if (e->request_code == X_DestroySubwindows) logmsg("X_DestroySubwindows");
    else if (e->request_code == X_ChangeSaveSet) logmsg("X_ChangeSaveSet");
    else if (e->request_code == X_ReparentWindow) logmsg("X_ReparentWindow");
    else if (e->request_code == X_MapWindow) logmsg("X_MapWindow");
    else if (e->request_code == X_MapSubwindows) logmsg("X_MapSubwindows");
    else if (e->request_code == X_UnmapWindow) logmsg("X_UnmapWindow");
    else if (e->request_code == X_UnmapSubwindows) logmsg("X_UnmapSubwindows");
    else if (e->request_code == X_ConfigureWindow) logmsg("X_ConfigureWindow");
    else if (e->request_code == X_CirculateWindow) logmsg("X_CirculateWindow");
    else if (e->request_code == X_GetGeometry) logmsg("X_GetGeometry");
    else if (e->request_code == X_QueryTree) logmsg("X_QueryTree");
    else if (e->request_code == X_InternAtom) logmsg("X_InternAtom");
    else if (e->request_code == X_GetAtomName) logmsg("X_GetAtomName");
    else if (e->request_code == X_ChangeProperty) logmsg("X_ChangeProperty");
    else if (e->request_code == X_DeleteProperty) logmsg("X_DeleteProperty");
    else if (e->request_code == X_GetProperty) logmsg("X_GetProperty");
    else if (e->request_code == X_ListProperties) logmsg("X_ListProperties");
    else if (e->request_code == X_SetSelectionOwner) logmsg("X_SetSelectionOwner");
    else if (e->request_code == X_GetSelectionOwner) logmsg("X_GetSelectionOwner");
    else if (e->request_code == X_ConvertSelection) logmsg("X_ConvertSelection");
    else if (e->request_code == X_SendEvent) logmsg("X_SendEvent");
    else if (e->request_code == X_GrabPointer) logmsg("X_GrabPointer");
    else if (e->request_code == X_UngrabPointer) logmsg("X_UngrabPointer");
    else if (e->request_code == X_GrabButton) logmsg("X_GrabButton");
    else if (e->request_code == X_UngrabButton) logmsg("X_UngrabButton");
    else if (e->request_code == X_ChangeActivePointerGrab) logmsg("X_ChangeActivePointerGrab");
    else if (e->request_code == X_GrabKeyboard) logmsg("X_GrabKeyboard");
    else if (e->request_code == X_UngrabKeyboard) logmsg("X_UngrabKeyboard");
    else if (e->request_code == X_GrabKey) logmsg("X_GrabKey");
    else if (e->request_code == X_UngrabKey) logmsg("X_UngrabKey");
    else if (e->request_code == X_AllowEvents) logmsg("X_AllowEvents");
    else if (e->request_code == X_GrabServer) logmsg("X_GrabServer");
    else if (e->request_code == X_UngrabServer) logmsg("X_UngrabServer");
    else if (e->request_code == X_QueryPointer) logmsg("X_QueryPointer");
    else if (e->request_code == X_GetMotionEvents) logmsg("X_GetMotionEvents");
    else if (e->request_code == X_TranslateCoords) logmsg("X_TranslateCoords");
    else if (e->request_code == X_WarpPointer) logmsg("X_WarpPointer");
    else if (e->request_code == X_SetInputFocus) logmsg("X_SetInputFocus");
    else if (e->request_code == X_GetInputFocus) logmsg("X_GetInputFocus");
    else if (e->request_code == X_QueryKeymap) logmsg("X_QueryKeymap");
    else if (e->request_code == X_OpenFont) logmsg("X_OpenFont");
    else if (e->request_code == X_CloseFont) logmsg("X_CloseFont");
    else if (e->request_code == X_QueryFont) logmsg("X_QueryFont");
    else if (e->request_code == X_QueryTextExtents) logmsg("X_QueryTextExtents");
    else if (e->request_code == X_ListFonts) logmsg("X_ListFonts");
    else if (e->request_code == X_ListFontsWithInfo) logmsg("X_ListFontsWithInfo");
    else if (e->request_code == X_SetFontPath) logmsg("X_SetFontPath");
    else if (e->request_code == X_GetFontPath) logmsg("X_GetFontPath");
    else if (e->request_code == X_CreatePixmap) logmsg("X_CreatePixmap");
    else if (e->request_code == X_FreePixmap) logmsg("X_FreePixmap");
    else if (e->request_code == X_CreateGC) logmsg("X_CreateGC");
    else if (e->request_code == X_ChangeGC) logmsg("X_ChangeGC");
    else if (e->request_code == X_CopyGC) logmsg("X_CopyGC");
    else if (e->request_code == X_SetDashes) logmsg("X_SetDashes");
    else if (e->request_code == X_SetClipRectangles) logmsg("X_SetClipRectangles");
    else if (e->request_code == X_FreeGC) logmsg("X_FreeGC");
    else if (e->request_code == X_ClearArea) logmsg("X_ClearArea");
    else if (e->request_code == X_CopyArea) logmsg("X_CopyArea");
    else if (e->request_code == X_CopyPlane) logmsg("X_CopyPlane");
    else if (e->request_code == X_PolyPoint) logmsg("X_PolyPoint");
    else if (e->request_code == X_PolyLine) logmsg("X_PolyLine");
    else if (e->request_code == X_PolySegment) logmsg("X_PolySegment");
    else if (e->request_code == X_PolyRectangle) logmsg("X_PolyRectangle");
    else if (e->request_code == X_PolyArc) logmsg("X_PolyArc");
    else if (e->request_code == X_FillPoly) logmsg("X_FillPoly");
    else if (e->request_code == X_PolyFillRectangle) logmsg("X_PolyFillRectangle");
    else if (e->request_code == X_PolyFillArc) logmsg("X_PolyFillArc");
    else if (e->request_code == X_PutImage) logmsg("X_PutImage");
    else if (e->request_code == X_GetImage) logmsg("X_GetImage");
    else if (e->request_code == X_PolyText8) logmsg("X_PolyText8");
    else if (e->request_code == X_PolyText16) logmsg("X_PolyText16");
    else if (e->request_code == X_ImageText8) logmsg("X_ImageText8");
    else if (e->request_code == X_ImageText16) logmsg("X_ImageText16");
    else if (e->request_code == X_CreateColormap) logmsg("X_CreateColormap");
    else if (e->request_code == X_FreeColormap) logmsg("X_FreeColormap");
    else if (e->request_code == X_CopyColormapAndFree) logmsg("X_CopyColormapAndFree");
    else if (e->request_code == X_InstallColormap) logmsg("X_InstallColormap");
    else if (e->request_code == X_UninstallColormap) logmsg("X_UninstallColormap");
    else if (e->request_code == X_ListInstalledColormaps) logmsg("X_ListInstalledColormaps");
    else if (e->request_code == X_AllocColor) logmsg("X_AllocColor");
    else if (e->request_code == X_AllocNamedColor) logmsg("X_AllocNamedColor");
    else if (e->request_code == X_AllocColorCells) logmsg("X_AllocColorCells");
    else if (e->request_code == X_AllocColorPlanes) logmsg("X_AllocColorPlanes");
    else if (e->request_code == X_FreeColors) logmsg("X_FreeColors");
    else if (e->request_code == X_StoreColors) logmsg("X_StoreColors");
    else if (e->request_code == X_StoreNamedColor) logmsg("X_StoreNamedColor");
    else if (e->request_code == X_QueryColors) logmsg("X_QueryColors");
    else if (e->request_code == X_LookupColor) logmsg("X_LookupColor");
    else if (e->request_code == X_CreateCursor) logmsg("X_CreateCursor");
    else if (e->request_code == X_CreateGlyphCursor) logmsg("X_CreateGlyphCursor");
    else if (e->request_code == X_FreeCursor) logmsg("X_FreeCursor");
    else if (e->request_code == X_RecolorCursor) logmsg("X_RecolorCursor");
    else if (e->request_code == X_QueryBestSize) logmsg("X_QueryBestSize");
    else if (e->request_code == X_QueryExtension) logmsg("X_QueryExtension");
    else if (e->request_code == X_ListExtensions) logmsg("X_ListExtensions");
    else if (e->request_code == X_ChangeKeyboardMapping) logmsg("X_ChangeKeyboardMapping");
    else if (e->request_code == X_GetKeyboardMapping) logmsg("X_GetKeyboardMapping");
    else if (e->request_code == X_ChangeKeyboardControl) logmsg("X_ChangeKeyboardControl");
    else if (e->request_code == X_GetKeyboardControl) logmsg("X_GetKeyboardControl");
    else if (e->request_code == X_Bell) logmsg("X_Bell");
    else if (e->request_code == X_ChangePointerControl) logmsg("X_ChangePointerControl");
    else if (e->request_code == X_GetPointerControl) logmsg("X_GetPointerControl");
    else if (e->request_code == X_SetScreenSaver) logmsg("X_SetScreenSaver");
    else if (e->request_code == X_GetScreenSaver) logmsg("X_GetScreenSaver");
    else if (e->request_code == X_ChangeHosts) logmsg("X_ChangeHosts");
    else if (e->request_code == X_ListHosts) logmsg("X_ListHosts");
    else if (e->request_code == X_SetAccessControl) logmsg("X_SetAccessControl");
    else if (e->request_code == X_SetCloseDownMode) logmsg("X_SetCloseDownMode");
    else if (e->request_code == X_KillClient) logmsg("X_KillClient");
    else if (e->request_code == X_RotateProperties) logmsg("X_RotateProperties");
    else if (e->request_code == X_ForceScreenSaver) logmsg("X_ForceScreenSaver");
    else if (e->request_code == X_SetPointerMapping) logmsg("X_SetPointerMapping");
    else if (e->request_code == X_GetPointerMapping) logmsg("X_GetPointerMapping");
    else if (e->request_code == X_SetModifierMapping) logmsg("X_SetModifierMapping");
    else if (e->request_code == X_GetModifierMapping) logmsg("X_GetModifierMapping");
    else if (e->request_code == X_NoOperation) logmsg("X_NoOperation");

    if (e->error_code == Success) logmsg("Success");
    if (e->error_code == BadRequest) logmsg("BadRequest");
    if (e->error_code == BadValue) logmsg("BadValue");
    if (e->error_code == BadWindow) logmsg("BadWindow");
    if (e->error_code == BadPixmap) logmsg("BadPixmap");
    if (e->error_code == BadAtom) logmsg("BadAtom");
    if (e->error_code == BadCursor) logmsg("BadCursor");
    if (e->error_code == BadFont) logmsg("BadFont");
    if (e->error_code == BadMatch) logmsg("BadMatch");
    if (e->error_code == BadDrawable) logmsg("BadDrawable");
    if (e->error_code == BadAccess) logmsg("BadAccess");
#endif

    return 0;
}
static void initAtoms() {
    WM_PROTOCOLS                    = XInternAtom(wmDisplay, "WM_PROTOCOLS", False);
    WM_DELETE_WINDOW                = XInternAtom(wmDisplay, "WM_DELETE_WINDOW", False);
    WM_STATE                        = XInternAtom(wmDisplay, "WM_STATE", False);
    WM_TAKE_FOCUS                   = XInternAtom(wmDisplay, "WM_TAKE_FOCUS", False);
    UTF8_STRING                     = XInternAtom(wmDisplay, "UTF8_STRING", False);
    _NET_SUPPORTED                  = XInternAtom(wmDisplay, "_NET_SUPPORTED", False);
    _NET_CLIENT_LIST                = XInternAtom(wmDisplay, "_NET_CLIENT_LIST", False);
    _NET_NUMBER_OF_DESKTOPS         = XInternAtom(wmDisplay, "_NET_NUMBER_OF_DESKTOPS", False);
    _NET_DESKTOP_GEOMETRY           = XInternAtom(wmDisplay, "_NET_DESKTOP_GEOMETRY", False);
    _NET_DESKTOP_VIEWPORT           = XInternAtom(wmDisplay, "_NET_DESKTOP_VIEWPORT", False);
    _NET_CURRENT_DESKTOP            = XInternAtom(wmDisplay, "_NET_CURRENT_DESKTOP", False);
    _NET_DESKTOP_NAMES              = XInternAtom(wmDisplay, "_NET_DESKTOP_NAMES", False);
    _NET_ACTIVE_WINDOW              = XInternAtom(wmDisplay, "_NET_ACTIVE_WINDOW", False);
    _NET_SUPPORTING_WM_CHECK        = XInternAtom(wmDisplay, "_NET_SUPPORTING_WM_CHECK", False);
    _NET_REQUEST_FRAME_EXTENTS      = XInternAtom(wmDisplay, "_NET_REQUEST_FRAME_EXTENTS", False);
    _NET_FRAME_EXTENTS              = XInternAtom(wmDisplay, "_NET_FRAME_EXTENTS", False);
    _NET_WM_NAME                    = XInternAtom(wmDisplay, "_NET_WM_NAME", False);
    _NET_WM_WINDOW_TYPE             = XInternAtom(wmDisplay, "_NET_WM_WINDOW_TYPE", False);
    _NET_WM_WINDOW_TYPE_DIALOG      = XInternAtom(wmDisplay, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    _NET_WM_STATE                   = XInternAtom(wmDisplay, "_NET_WM_STATE", False);
    _NET_WM_STATE_STICKY            = XInternAtom(wmDisplay, "_NET_WM_STATE_STICKY", False);
    _NET_WM_STATE_HIDDEN            = XInternAtom(wmDisplay, "_NET_WM_STATE_HIDDEN", False);
    _NET_WM_STATE_FULLSCREEN        = XInternAtom(wmDisplay, "_NET_WM_STATE_FULLSCREEN", False);
    _NET_WM_ALLOWED_ACTIONS         = XInternAtom(wmDisplay, "_NET_WM_ALLOWED_ACTIONS", False);
    _NET_WM_ACTION_FULLSCREEN       = XInternAtom(wmDisplay, "_NET_WM_ACTION_FULLSCREEN", False);

    Atom supported[] = {
            _NET_SUPPORTED,
            _NET_CLIENT_LIST,
            _NET_NUMBER_OF_DESKTOPS,
            _NET_DESKTOP_GEOMETRY,
            _NET_DESKTOP_VIEWPORT,
            _NET_CURRENT_DESKTOP,
            _NET_DESKTOP_NAMES,
            _NET_ACTIVE_WINDOW,
            _NET_SUPPORTING_WM_CHECK,
            _NET_REQUEST_FRAME_EXTENTS,
            _NET_FRAME_EXTENTS,
            _NET_WM_NAME,
            _NET_WM_WINDOW_TYPE,
            _NET_WM_WINDOW_TYPE_DIALOG,
            _NET_WM_STATE,
            _NET_WM_STATE_STICKY,
            _NET_WM_STATE_HIDDEN,
            _NET_WM_STATE_FULLSCREEN,
            _NET_WM_ALLOWED_ACTIONS,
            _NET_WM_ACTION_FULLSCREEN
    };

    XChangeProperty(wmDisplay, wmRoot, _NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char*)supported, LENGTH(supported));
    XDeleteProperty(wmDisplay, wmRoot, _NET_CLIENT_LIST);

    const char wmname[] = WINDOW_MANAGER_NAME;
    wmcheckwin = XCreateSimpleWindow(wmDisplay, wmRoot, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(wmDisplay, wmcheckwin, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(wmDisplay, wmcheckwin, _NET_WM_NAME, UTF8_STRING, 8, PropModeReplace, (unsigned char *) wmname, LENGTH(wmname));
    XChangeProperty(wmDisplay, wmRoot, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *) &wmcheckwin, 1);

    long geometry[] = { wmScreenWidth, wmScreenHeight };
    XChangeProperty(wmDisplay, wmRoot, _NET_DESKTOP_GEOMETRY, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)geometry, 2);

    long viewport[] = { 0, 0 };
    XChangeProperty(wmDisplay, wmRoot, _NET_DESKTOP_VIEWPORT, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)viewport, 2);

    long currentDesktop = wmActiveWorkspace;
    XChangeProperty(wmDisplay, wmRoot, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&currentDesktop, 1);

    updateWorkspaceAtoms();
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
                if (!attr.override_redirect && attr.map_state == IsViewable) {
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
    XSelectInput(wmDisplay, wmRoot, StructureNotifyMask | SubstructureRedirectMask | SubstructureNotifyMask);
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

    wmCursors[CURSOR_DEFAULT]               = XCreateFontCursor(wmDisplay, XC_arrow);
    wmCursors[CURSOR_DRAG]                  = XCreateFontCursor(wmDisplay, XC_fleur);
    wmCursors[CURSOR_RESIZE_TOP_LEFT]       = XCreateFontCursor(wmDisplay, XC_top_left_corner);
    wmCursors[CURSOR_RESIZE_TOP]            = XCreateFontCursor(wmDisplay, XC_top_side);
    wmCursors[CURSOR_RESIZE_TOP_RIGHT]      = XCreateFontCursor(wmDisplay, XC_top_right_corner);
    wmCursors[CURSOR_RESIZE_RIGHT]          = XCreateFontCursor(wmDisplay, XC_right_side);
    wmCursors[CURSOR_RESIZE_BOTTOM_RIGHT]   = XCreateFontCursor(wmDisplay, XC_bottom_right_corner);
    wmCursors[CURSOR_RESIZE_BOTTOM]         = XCreateFontCursor(wmDisplay, XC_bottom_side);
    wmCursors[CURSOR_RESIZE_BOTTOM_LEFT]    = XCreateFontCursor(wmDisplay, XC_bottom_left_corner);
    wmCursors[CURSOR_RESIZE_LEFT]           = XCreateFontCursor(wmDisplay, XC_left_side);
    XDefineCursor(wmDisplay, wmRoot, wmCursors[CURSOR_DEFAULT]);

    wmCreateBar();
    wmUpdateBounds();

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
        XRemoveFromSaveSet(wmDisplay, wmWindow->window);
        XReparentWindow(wmDisplay, wmWindow->window, wmRoot, 0, 0);
        XDestroyWindow(wmDisplay, wmWindow->frame);
        free(wmWindow);
    }
    XUngrabServer(wmDisplay);

    for (int i = 0; i < CURSOR_LAST; i++) {
        XFreeCursor(wmDisplay, wmCursors[i]);
    }

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
            if (source->fullscreen == source->activeWindow) {
                hideFullscreenWindow(source);
                if (destination->fullscreen && destination->fullscreen != source->fullscreen) {
                    unsetFullscreen(destination->fullscreen);
                }
                destination->fullscreen = source->activeWindow;
                source->fullscreen = NULL;
            }

            unsigned* workspaces = &source->activeWindow->workspaces;

            source->showSplitBorder = 0;
            source->countWindows--;
            removeWindowFromLayout(source, source->activeWindow);
            *workspaces ^= 1U << wmActiveWorkspace;

            if (!(*workspaces & (1U << workspace))) {
                destination->showSplitBorder = 0;
                destination->countWindows++;
                addWindowToLayout(destination, source->activeWindow);
                *workspaces |= 1U << workspace;

                destination->activeWindow = source->activeWindow;
            }

            setActiveWindow(source, wmNextVisibleWindow(wmActiveWorkspace));
            updateWorkspaceAtoms();
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
            if (activeWindow->fullscreen) {
                if (workspace->fullscreen) {
                    unsetFullscreen(workspace->fullscreen);
                }
                workspace->fullscreen = activeWindow;
            }

            workspace->countWindows++;
            addWindowToLayout(workspace, activeWindow);
            workspace->activeWindow = activeWindow;
        }
        else {
            if (workspace->fullscreen == workspace->activeWindow) {
                if (workspaceIndex == wmActiveWorkspace) {
                    hideFullscreenWindow(workspace);
                }
                workspace->fullscreen = NULL;
            }

            workspace->countWindows--;
            removeWindowFromLayout(workspace, activeWindow);
            setActiveWindow(workspace, wmNextVisibleWindow(workspaceIndex));
        }
        updateWorkspaceAtoms();

        wmWorkspaces[wmActiveWorkspace].showSplitBorder = 0;
        wmUpdateBorders();

        if (workspaceIndex == wmActiveWorkspace) {
            wmShowActiveWorkspace();
        }
    }
}

void wmFocusWindow(wmWindow* window) {
    unsigned workspaces = (window->floating && window->floating->flags & FLOATING_STICKY) ? ~0 : window->workspaces;
    if ((workspaces & 1 << wmActiveWorkspace) == 0) {
        return;
    }

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    setActiveWindow(workspace, window);
    if (workspace->layout) {
        workspace->showSplitBorder = 0;
        wmNode* split = findNode(workspace->layout, workspace->activeWindow);
        if (split) {
            workspace->splitNode = split;
        }
    }
    XSetInputFocus(wmDisplay, window->window, RevertToPointerRoot, CurrentTime);
    wmUpdateBorders();
}
void wmRequestCloseWindow(wmWindow* window) {
    XRemoveFromSaveSet(wmDisplay, window->window);

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
    XAddToSaveSet(wmDisplay, window);

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    workspace->countWindows++;

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

    Atom allowed[] = { _NET_WM_ACTION_FULLSCREEN };
    XChangeProperty(wmDisplay, window, _NET_WM_ALLOWED_ACTIONS, XA_ATOM, 32, PropModeReplace, (unsigned char*)allowed, LENGTH(allowed));

    XGrabButton(wmDisplay, Button1, 0, window, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
    XGrabButton(wmDisplay, Button1, MOD, window, False, ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, None);
    XGrabButton(wmDisplay, Button3, MOD, window, False, ButtonMotionMask, GrabModeAsync, GrabModeAsync, None, None);

    XSelectInput(wmDisplay, frame, SubstructureNotifyMask | SubstructureRedirectMask);
    XSelectInput(wmDisplay, window, EnterWindowMask | FocusChangeMask);
    XReparentWindow(wmDisplay, window, frame, 0, 0);
    XMapWindow(wmDisplay, window);

    wmWindow* new_wmWindow = calloc(1, sizeof(wmWindow));
    new_wmWindow->window = window;
    new_wmWindow->frame = frame;
    new_wmWindow->workspaces = 1 << wmActiveWorkspace;
    attachWindow(new_wmWindow);

    int maxw, maxh;
    int minw, minh;
    XSizeHints hints;
    long l;
    if (XGetWMNormalHints(wmDisplay, window, &hints, &l)) {
        if (hints.flags & PMaxSize) {
            maxw = hints.max_width;
            maxh = hints.max_height;
        }
        if (hints.flags & PMinSize) {
            minw = hints.min_width;
            minh = hints.min_height;
        }
    }

    Window parent;
    int dialog =
            (hints.flags & (PMaxSize | PMinSize) && maxw == minw && maxh == minh) ||
            XGetTransientForHint(wmDisplay, window, &parent) ||
            containsAtomValue(window, _NET_WM_WINDOW_TYPE, _NET_WM_WINDOW_TYPE_DIALOG, _NET_WM_WINDOW_TYPE_SUPPORTED_COUNT);
    int sticky = containsAtomValue(window, _NET_WM_STATE, _NET_WM_STATE_STICKY, _NET_WM_STATE_SUPPORTED_COUNT);
    if (dialog || sticky) {
        int floatingWindowBorderWidth = borderWidth;

        // Check motif hints
        // Reference: https://github.com/i3/i3/blob/next/src/window.c#L415
        {
            Atom _MOTIF_WM_HINTS = XInternAtom(wmDisplay, "_MOTIF_WM_HINTS", False);

            Atom type_return;
            int format_return;
            unsigned long nitems;
            unsigned long bytes_after_return;
            unsigned long* motif_hints;
            Status status = XGetWindowProperty(wmDisplay, window, _MOTIF_WM_HINTS, 0, 5, False, AnyPropertyType, &type_return, &format_return, &nitems, &bytes_after_return, (unsigned char**)&motif_hints);
            if (status == Success) {
                /* This implementation simply mirrors Gnome's Metacity. Official
                 * documentation of this hint is nowhere to be found.
                 * For more information see:
                 * https://people.gnome.org/~tthurman/docs/metacity/xprops_8h-source.html
                 * https://stackoverflow.com/questions/13787553/detect-if-a-x11-window-has-decorations
                 */
#define MWM_HINTS_FLAGS_FIELD 0
#define MWM_HINTS_DECORATIONS_FIELD 2

#define MWM_HINTS_DECORATIONS (1 << 1)
#define MWM_DECOR_ALL (1 << 0)
#define MWM_DECOR_BORDER (1 << 1)
#define MWM_DECOR_TITLE (1 << 3)
                /* The property consists of an array of 5 uint32_t's. The first value is a
                 * bit mask of what properties the hint will specify. We are only interested
                 * in MWM_HINTS_DECORATIONS because it indicates that the third value of the
                 * array tells us which decorations the window should have, each flag being
                 * a particular decoration. Notice that X11 (Xlib) often mentions 32-bit
                 * fields which in reality are implemented using unsigned long variables
                 * (64-bits long on amd64 for example). On the other hand,
                 * xcb_get_property_value() behaves strictly according to documentation,
                 * i.e. returns 32-bit data fields.
                 */
                if (nitems == 5 && motif_hints[MWM_HINTS_FLAGS_FIELD] & MWM_HINTS_DECORATIONS) {
                    if (motif_hints[MWM_HINTS_DECORATIONS_FIELD] == 0) {
                        floatingWindowBorderWidth = 0;
                    }
                }
#undef MWM_HINTS_FLAGS_FIELD
#undef MWM_HINTS_DECORATIONS_FIELD
#undef MWM_HINTS_DECORATIONS
#undef MWM_DECOR_ALL
#undef MWM_DECOR_BORDER
#undef MWM_DECOR_TITLE
                XFree(motif_hints);
            }
        }

        setFloatingWindow(new_wmWindow, attributes, floatingWindowBorderWidth);
        if (dialog) new_wmWindow->floating->flags |= FLOATING_DIALOG;
        if (sticky) new_wmWindow->floating->flags |= FLOATING_STICKY;
        XRaiseWindow(wmDisplay, frame);
    }
    else {
        XLowerWindow(wmDisplay, frame);
    }

    addWindowToLayout(workspace, new_wmWindow);
    setActiveWindow(workspace, new_wmWindow);
    updateWorkspaceAtoms();
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
            workspace->countWindows--;
            wmWindow* activeWindow = workspace->activeWindow;
            if (activeWindow == window) {
                setActiveWindow(workspace, wmNextVisibleWindow(i));
            }
            removeWindowFromLayout(workspace, window);

            if (workspace->fullscreen == window) {
                workspace->fullscreen = NULL;
            }
        }
    }

    if (window->floating) {
        unsetFloatingWindow(window);
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
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (workspace->fullscreen) {
        hideFullscreenWindow(workspace);
    }

    wmActiveWorkspace = workspaceIndex;

    workspace = &wmWorkspaces[wmActiveWorkspace];
    if (workspace->fullscreen) {
        showFullscreenWindow(workspace);
    }

    long currentDesktop = wmActiveWorkspace;
    XChangeProperty(wmDisplay, wmRoot, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char*)&currentDesktop, 1);

    updateWorkspaceAtoms();

    setActiveWindow(workspace, workspace->activeWindow);
    workspace->showSplitBorder = 0;
    wmUpdateBorders();
    wmShowActiveWorkspace();
    wmUpdateBar();
}
void wmShowActiveWorkspace() {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    wmWindow* activeWindow = workspace->activeWindow;
    if (workspace->fullscreen) {
        XUnmapWindow(wmDisplay, wmBarWindow);
        XMoveResizeWindow(wmDisplay, workspace->fullscreen->frame, 0, 0, wmScreenWidth, wmScreenHeight);
        XMoveResizeWindow(wmDisplay, workspace->fullscreen->window, 0, 0, wmScreenWidth, wmScreenHeight);
        for (wmWindow* window = wmHead; window; window = window->next) {
            if (!window->fullscreen) {
                long data[2] = { None, None };
                XUnmapWindow(wmDisplay, window->frame);
                data[0] = IconicState;
                addAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT);
                XChangeProperty(wmDisplay, window->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
            }
        }
    }
    else {
        XMapWindow(wmDisplay, wmBarWindow);
        int mask = 1 << wmActiveWorkspace;
        for (wmWindow* window = wmHead; window; window = window->next) {
            long data[2] = { None, None };
            if (window->floating && window->floating->flags & FLOATING_STICKY) {
                XMapWindow(wmDisplay, window->frame);
                data[0] = NormalState;
                Atom newAtoms[_NET_WM_STATE_SUPPORTED_COUNT - 1];
                removeAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT, newAtoms);

                if (!activeWindow) {
                    activeWindow = window;
                }
            }
            else if (window->workspaces & mask) {
                XMapWindow(wmDisplay, window->frame);
                data[0] = NormalState;
                Atom newAtoms[_NET_WM_STATE_SUPPORTED_COUNT - 1];
                removeAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT, newAtoms);
            }
            else {
                XUnmapWindow(wmDisplay, window->frame);
                data[0] = IconicState;
                addAtom(window->window, _NET_WM_STATE, _NET_WM_STATE_HIDDEN, _NET_WM_STATE_SUPPORTED_COUNT);
            }

            XChangeProperty(wmDisplay, window->window, WM_STATE, WM_STATE, 32, PropModeReplace, (unsigned char*)data, 2);
        }

        wmNode* layout = workspace->layout;
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

            XWindowChanges wc;
            wc.x = 0;
            wc.y = 0;
            wc.width = wmScreenWidth;
            wc.height = height;
            XConfigureWindow(wmDisplay, layout->window->window, CWX | CWY | CWWidth | CWHeight, &wc);
            XMoveResizeWindow(wmDisplay, layout->window->frame, 0, y, wmScreenWidth, height);
            configureWindow(layout->window->window, 0, y, wmScreenWidth, height, 0);
        }
        else {
            wmUpdateMouseCoords();
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
    if (workspace->fullscreen) {
        XSetWindowBorderWidth(wmDisplay, workspace->fullscreen->frame, 0);
        return;
    }

    for (wmFloatingWindow* d = wmFloatingWindows; d; d = d->next) {
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
    wmFloatingWindow* dialog = window->floating;
    if (dialog) {
        configureWindow(window->window, dialog->x, dialog->y, dialog->width, dialog->height, dialog->borderWidth);
        return;
    }

    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (!workspace->layout) {
        return;
    }

    wmNode* node = findNode(workspace->layout, window);
    if (node) {
        configureWindow(window->window, node->x, node->y, node->width, node->height, borderWidth);
    }
}
void wmUpdateBounds() {
    wmWindowAreaX = gap;
#ifdef bottomBar
    wmWindowAreaY = gap;
#else
    wmWindowAreaY = gap + wmBarHeight;
#endif
    wmWindowAreaWidth = wmScreenWidth - 2 * gap;
    wmWindowAreaHeight = wmScreenHeight - 2 * gap - wmBarHeight;
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

void wmUpdateMouseCoords() {
    Window root_return, child_return;
    int d;
    XQueryPointer(wmDisplay, wmRoot, &root_return, &child_return, &wmMouseX, &wmMouseY, &d, &d, (unsigned*)&d);
}

void wmToggleFullscreen() {
    wmWorkspace* workspace = &wmWorkspaces[wmActiveWorkspace];
    if (workspace->activeWindow) {
        XEvent e;
        e.type = ClientMessage;
        e.xclient.window = workspace->activeWindow->window;
        e.xclient.message_type = _NET_WM_STATE;
        e.xclient.format = 32;
        e.xclient.data.l[0] = _NET_WM_STATE_TOGGLE;
        e.xclient.data.l[1] = _NET_WM_STATE_FULLSCREEN;
        e.xclient.data.l[2] = 0;
        e.xclient.data.l[3] = 0;
        XSendEvent(wmDisplay, wmRoot, False, SubstructureNotifyMask | SubstructureRedirectMask, &e);
    }
}
