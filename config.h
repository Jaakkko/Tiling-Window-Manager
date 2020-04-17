//
// Created by jaakko on 9.4.2020.
//
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <X11/keysym.h>
#include <X11/X.h>

#include "instance.h"
#include "input.h"
#include "api.h"

#define WINDOW_MANAGER_NAME "X11 Window Manager"

//                                          AARRGGBB
static const unsigned borderColorActive = 0xfff7eb60;
static const unsigned borderColor       = 0x801d5b82;
static const unsigned borderWidth       = 2;
static const unsigned gap               = 4; // 1 = 2px

#define MOD Mod4Mask

#define WORKSPACE_COUNT 9
#define WORKSPACE(X) \
    { MOD,               XK_##X, selectWorkspace,   { .i = (X - 1) } }, \
    { MOD | ShiftMask,   XK_##X, moveToWorkspace,   { .i = (X - 1) } }, \
    { MOD | ControlMask, XK_##X, toggleToWorkspace, { .i = (X - 1) } }, \

static const char* startupScriptBath = "~/.config/wm/startup.sh";

static const KeyBinding keyBindings[] = {
        // Modifier                    Key          Function            Argument
        { MOD | ControlMask,  XK_Q,        quit,                   { .i = 130             } },
        { MOD | ShiftMask,    XK_Q,        quit,                   { .i = 0               } },
        { MOD,                XK_Q,        closeActiveWindow,      0                   },
        { MOD,                XK_J,        focus,                  { .i = +1              } },
        { MOD,                XK_K,        focus,                  { .i = -1              } },
        { MOD,                XK_Return,   openApplication,        { .v = "alacritty"     } },
        { MOD,                XK_C,        setSplitOrientation,    { .i = NODE_HORIZONTAL } },
        { MOD,                XK_V,        setSplitOrientation,    { .i = NODE_VERTICAL   } },

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
