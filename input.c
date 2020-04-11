//
// Created by jaakko on 9.4.2020.
//

#include "input.h"
#include "util.h"
#include "config.h"
#include "instance.h"

#include <X11/Xlib.h>

void initializeKeyBindings() {
    for (int i = 0; i < LENGTH(keyBindings); i++) {
        KeyBinding keyBinding = keyBindings[i];
        XGrabKey(wmDisplay, XKeysymToKeycode(wmDisplay, keyBinding.key), keyBinding.modifiers, wmRoot, False, GrabModeAsync, GrabModeAsync);
    }
}

void freeKeyBindings() {
    for (int i = 0; i < LENGTH(keyBindings); i++) {
        KeyBinding keyBinding = keyBindings[i];
        XUngrabKey(wmDisplay, XKeysymToKeycode(wmDisplay, keyBinding.key), keyBinding.modifiers, wmRoot);
    }
}
