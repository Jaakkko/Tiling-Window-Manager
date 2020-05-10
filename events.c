//
// Created by jaakko on 9.4.2020.
//
#include "events.h"
#include "instance.h"
#include "config.h"
#include "input.h"
#include "util.h"
#include "bar/bar.h"

void wmKeyPress(XEvent event) {
    for (int i = 0; i < LENGTH(keyBindings); i++) {
        KeyBinding keyBinding = keyBindings[i];
        if (keyBinding.modifiers == event.xkey.state && XKeysymToKeycode(wmDisplay, keyBinding.key) == event.xkey.keycode) {
            keyBinding.func(keyBinding.arg);
            break;
        }
    }
}

void wmButtonPress(XEvent event) {
    XButtonEvent* e = &event.xbutton;
    if (e->state == 0 && e->button == Button1) {
        wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
        if (activeWindow && activeWindow->window != e->window) {
            wmWindow* window = wmWindowTowmWindow(e->window);
            if (window && wmWorkspaces[wmActiveWorkspace].activeWindow != window) {
                wmFocusWindow(window);
            }
        }
        XAllowEvents(wmDisplay, ReplayPointer, CurrentTime);
    }
    else if (e->state == MOD) {
        wmWindow* w = wmWindowTowmWindow(e->window);
        if (!w || !w->floating) {
            return;
        }

        if (e->button == Button1) {
            if (XGrabPointer(wmDisplay, wmRoot, False, ButtonReleaseMask | ButtonPressMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, wmCursors[CURSOR_DRAG], CurrentTime) != GrabSuccess) {
                return;
            }

            wmUpdateMouseCoords();
            int lastX, lastY;
            XEvent ev;
            do {
                XMaskEvent(wmDisplay, ButtonReleaseMask | ButtonPressMask | PointerMotionMask, &ev);
                switch (ev.type) {
                    case MotionNotify:
                        lastX = wmMouseX;
                        lastY = wmMouseY;
                        wmUpdateMouseCoords();
                        w->floating->x = w->floating->x + wmMouseX - lastX;
                        w->floating->y = w->floating->y + wmMouseY - lastY;
                        XMoveWindow(wmDisplay, w->frame, w->floating->x, w->floating->y);
                        break;
                }
            }
            while (ev.type != ButtonRelease);
            XUngrabPointer(wmDisplay, CurrentTime);
        }
        else if (e->button == Button3) {
            wmUpdateMouseCoords();
            int left = (wmMouseX - w->floating->x) < RESIZE_DISTANCE;
            int right = (w->floating->x + w->floating->width - wmMouseX) < RESIZE_DISTANCE;
            int top = (wmMouseY - w->floating->y) < RESIZE_DISTANCE;
            int bottom = (w->floating->y + w->floating->height - wmMouseY) < RESIZE_DISTANCE;
            int cursor = CURSOR_LAST;
            if (left && top)
                cursor = CURSOR_RESIZE_TOP_LEFT;
            else if (right && top)
                cursor = CURSOR_RESIZE_TOP_RIGHT;
            else if (right && bottom)
                cursor = CURSOR_RESIZE_BOTTOM_RIGHT;
            else if (bottom && left)
                cursor = CURSOR_RESIZE_BOTTOM_LEFT;
            else if (left)
                cursor = CURSOR_RESIZE_LEFT;
            else if (right)
                cursor = CURSOR_RESIZE_RIGHT;
            else if (top)
                cursor = CURSOR_RESIZE_TOP;
            else if (bottom)
                cursor = CURSOR_RESIZE_BOTTOM;

            if (cursor == CURSOR_LAST) {
                return;
            }

            if (XGrabPointer(wmDisplay, wmRoot, False, ButtonReleaseMask | ButtonPressMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, None, wmCursors[cursor], CurrentTime) != GrabSuccess) {
                return;
            }

            int lastX, lastY;
            int dx, dy;
            XEvent ev;
            do {
                XMaskEvent(wmDisplay, ButtonReleaseMask | ButtonPressMask | PointerMotionMask, &ev);
                switch (ev.type) {
                    case MotionNotify:
                        lastX = wmMouseX;
                        lastY = wmMouseY;
                        wmUpdateMouseCoords();
                        dx = wmMouseX - lastX;
                        dy = wmMouseY - lastY;
                        w->floating->x += dx * left;
                        w->floating->y += dy * top;
                        w->floating->width += dx * (right - left);
                        w->floating->height += dy * (bottom - top);
                        XMoveResizeWindow(wmDisplay, w->frame, w->floating->x, w->floating->y, w->floating->width, w->floating->height);
                        XWindowChanges wc;
                        wc.x = 0;
                        wc.y = 0;
                        wc.width = w->floating->width;
                        wc.height = w->floating->height;
                        XConfigureWindow(wmDisplay, w->window, CWX | CWY | CWWidth | CWHeight, &wc);
                        wmConfigureWindow(w);
                        break;
                }
            }
            while (ev.type != ButtonRelease);
            XUngrabPointer(wmDisplay, CurrentTime);
        }
    }
}

void wmEnterNotify(XEvent event) {
    int lastMouseX = wmMouseX;
    int lastMouseY = wmMouseY;
    wmUpdateMouseCoords();
    if (lastMouseX == wmMouseX && lastMouseY == wmMouseY) {
        return;
    }

    XCrossingEvent* ev = &event.xcrossing;
    if (ev->mode == NotifyNormal && ev->detail != NotifyInferior) {
        wmWindow* window = wmWindowTowmWindow(ev->window);
        if (window) {
            wmFocusWindow(window);
        }
    }
}

void wmExpose(XEvent event) {
    XExposeEvent* e = &event.xexpose;
    if (e->window == wmBarWindow) {
        if (e->x == 0 && e->y == 0 && e->width == wmScreenWidth && e->height == wmBarHeight) {
            wmExposeBar();
        }

        wmUpdateBar();
    }
}

void wmConfigureNotify(XEvent event) {
    XConfigureEvent* e = &event.xconfigure;
    if (e->window == wmRoot) {
        wmScreenWidth = e->width;
        wmScreenHeight = e->height;

        wmUpdateBounds();
        wmUpdateBarBounds();
        wmShowActiveWorkspace();
    }
}

void wmConfigureRequest(XEvent event) {
    XConfigureRequestEvent* ev = &event.xconfigurerequest;
    wmWindow* window = wmWindowTowmWindow(ev->window);
    if (window) {
        wmConfigureWindow(window);
    }
    else {
        XWindowChanges c;
        c.x = ev->x;
        c.y = ev->y;
        c.width = ev->width;
        c.height = ev->height;
        c.border_width = ev->border_width;
        c.sibling = ev->above;
        c.stack_mode = ev->detail;
        XConfigureWindow(wmDisplay, ev->window, ev->value_mask, &c);
        XSync(wmDisplay, False);
    }
}

void wmUnmapNotify(XEvent event) {
    XUnmapEvent* e = &event.xunmap;
    if (e->event == wmRoot) {
        return;
    }

    wmWindow* window = wmWindowTowmWindow(e->window);
    if (window) {
        Window root, parent;
        Window* children;
        unsigned nchildren;
        if (XQueryTree(wmDisplay, window->frame, &root, &parent, &children, &nchildren) && nchildren) {
            XReparentWindow(wmDisplay, e->window, wmRoot, 0, 0);
        }
        wmFreeWindow(window);
    }
}

void wmMapRequest(XEvent event) {
    Window window = event.xmaprequest.window;

    // Don`t map twice
    if (wmWindowTowmWindow(window)) {
        return;
    }

    XWindowAttributes attr;
    if (XGetWindowAttributes(wmDisplay, window, &attr)) {
        wmNewWindow(window, &attr);
        wmShowActiveWorkspace();
    }
}

void wmClientMessage(XEvent event) {
    XClientMessageEvent* message = &event.xclient;
    for (int i = 0; i < clientMessageHandlersCount; i++) {
        if (message->message_type == *clientMessageHandler[i].atom) {
            clientMessageHandler[i].handler(message);
            break;
        }
    }
}

void (*handler[LASTEvent])(XEvent) = {
        [KeyPress] = wmKeyPress,
        [ButtonPress] = wmButtonPress,
        [EnterNotify] = wmEnterNotify,
        [Expose] = wmExpose,
        [ConfigureNotify] = wmConfigureNotify,
        [ConfigureRequest] = wmConfigureRequest,
        [UnmapNotify] = wmUnmapNotify,
        [MapRequest] = wmMapRequest,
        [ClientMessage] = wmClientMessage,
};
