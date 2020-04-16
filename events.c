//
// Created by jaakko on 9.4.2020.
//
#include "events.h"
#include "instance.h"
#include "config.h"
#include "input.h"
#include "util.h"

void wmKeyPress(XEvent event) {
    for (int i = 0; i < LENGTH(keyBindings); i++) {
        KeyBinding keyBinding = keyBindings[i];
        if (keyBinding.modifiers == event.xkey.state && XKeysymToKeycode(wmDisplay, keyBinding.key) == event.xkey.keycode) {
            keyBinding.func(keyBinding.arg);
            break;
        }
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
        [MapRequest] = wmMapRequest,
        [DestroyNotify] = wmDestroyNotify,
        [ClientMessage] = wmClientMessage,
};
