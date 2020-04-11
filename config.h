//
// Created by jaakko on 9.4.2020.
//
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <X11/keysym.h>
#include <X11/X.h>

#include "input.h"
#include "api.h"

#define MOD Mod4Mask

static const KeyBinding keyBindings[] = {
        // Modifier                  Key          Function            Argument
        { MOD | ShiftMask,  XK_Q,        quit,               0                    },
        { MOD,              XK_Q,        closeActiveWindow,  0                    },
        { MOD,              XK_J,        focus,              { .i = +1          } },
        { MOD,              XK_K,        focus,              { .i = -1          } },
        { MOD,              XK_Return,   openApplication,    { .v = "alacritty" } },
};

#endif
