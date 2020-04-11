//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_KEYBINDIGS_H
#define WM_KEYBINDIGS_H

#include "api.h"

typedef struct {
    const unsigned int modifiers;
    const unsigned int key;
    void (*func)(const Arg);
    const Arg arg;
} KeyBinding;

void initializeKeyBindings();
void freeKeyBindings();

#endif //WM_KEYBINDIGS_H
