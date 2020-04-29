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
}

void wmEnterNotify(XEvent event) {
    if (wmSkipNextEnterNotify) {
        wmSkipNextEnterNotify = 0;
        return;
    }

    XCrossingEvent* ev = &event.xcrossing;
    if (ev->mode == NotifyNormal) {
        wmWindow* window = wmWindowTowmWindow(ev->window);
        if (window) {
            wmFocusWindow(window);
        }
    }
}

void wmExpose(XEvent event) {
    if (event.xexpose.window == wmBarWindow) {
        wmUpdateBar();
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

void wmDestroyNotify(XEvent event) {
    wmWindow* window = wmWindowTowmWindow(event.xdestroywindow.window);
    if (window) {
        wmFreeWindow(window);
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
        [ConfigureRequest] = wmConfigureRequest,
        [MapRequest] = wmMapRequest,
        [DestroyNotify] = wmDestroyNotify,
        [ClientMessage] = wmClientMessage,
};
