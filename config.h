//
// Created by jaakko on 9.4.2020.
//
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <X11/keysym.h>
#include <X11/X.h>

#include "input.h"
#include "api.h"

#define WINDOW_MANAGER_NAME "X11 Window Manager"

#define MOD Mod4Mask

#define WORKSPACE_COUNT 9
#define WORKSPACE(X) \
    { MOD,               XK_##X, selectWorkspace,   { .i = (X - 1) } }, \
    { MOD | ShiftMask,   XK_##X, moveToWorkspace,   { .i = (X - 1) } }, \
    { MOD | ControlMask, XK_##X, toggleToWorkspace, { .i = (X - 1) } }, \

static const char* startupScriptBath = "~/.config/wm/startup.sh";

static const KeyBinding keyBindings[] = {
        // Modifier                    Key          Function            Argument
        { MOD | ControlMask,  XK_Q,        quit,               { .i = 130         } },
        { MOD | ShiftMask,    XK_Q,        quit,               { .i = 0           } },
        { MOD,                XK_Q,        closeActiveWindow,  0                    },
        { MOD,                XK_J,        focus,              { .i = -1          } },
        { MOD,                XK_K,        focus,              { .i = +1          } },
        { MOD,                XK_Return,   openApplication,    { .v = "alacritty" } },

        WORKSPACE(1)
        WORKSPACE(2)
        WORKSPACE(3)
        WORKSPACE(4)
        WORKSPACE(5)
        WORKSPACE(6)
        WORKSPACE(7)
        WORKSPACE(8)
        WORKSPACE(9)
};

#endif
