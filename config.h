//
// Created by jaakko on 9.4.2020.
//
#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <X11/keysym.h>
#include <X11/X.h>

#include "bar/block.h"
#include "instance.h"
#include "input.h"
#include "api.h"

#define WINDOW_MANAGER_NAME "X11 Window Manager"

static const float    resizeChange = 0.1;
static const unsigned minWidth     = 200;
static const unsigned minHeight    = 200;

//                                          AARRGGBB
static const unsigned borderColorSplit          = 0xff50fa78;
static const unsigned borderColorActive         = 0xffe5e5e5;
static const unsigned borderColor               = 0x00000000;
static const unsigned borderWidth               = 2;
static const unsigned gap                       = 4; // 1 = 2px
#define smartGaps

// Bar
static const char* barFont                      = "Roboto:style=Regular:size=12:antialias=true";
static const unsigned barPadding                = 4;
static const unsigned barBackground             = 0xDB000000;
static const unsigned barBackgroundSelected     = 0xFFc7c99b;
static const unsigned barBackgroundUnselected   = 0xFF000000;
static const unsigned barForegroundSelected     = 0xFF14213D;
static const unsigned barForegroundUnselected   = 0xFFBFD4E1;
#define bottomBar

// Bar blocks
#define blockBackround
static const unsigned blockBackgroundColor      = 0xFF14213D;
static const unsigned blockForegroundColor      = 0xFFBFD4E1;
static const unsigned blockPadding              = 8;
static const unsigned blockMargin               = 0;
static wmBlock blocks[] = {
        // func          longest possible
        { datetime,     "10.10.2020 14.14" },
        { disk,         "345 G"            },
        { temperature,  "99,5 °C"          },
        { cpu,          "99,99 %"          },
        { ram,          "34,4 G"           },
        { battery,      "100%"             },
};

#define MOD Mod4Mask

#define WORKSPACE_COUNT 9
#define WORKSPACE(X) \
    { MOD,               XK_##X, selectWorkspace,   { .i = (X - 1) } }, \
    { MOD | ShiftMask,   XK_##X, moveToWorkspace,   { .i = (X - 1) } }, \
    { MOD | ControlMask, XK_##X, toggleToWorkspace, { .i = (X - 1) } }, \

static const char* startupScriptBath = "~/.config/wm/startup.sh";

static const char* rofi[] = { "rofi", "-show", "run", NULL };
static const char* alacritty[] = { "alacritty", NULL };
static const char* chromium[] = { "chromium", "--force-dark-mode", NULL };
static const char* suspend[] = { "systemctl", "suspend", NULL };

static const KeyBinding keyBindings[] = {
        // Modifier                    Key          Function                Argument
        { MOD | ControlMask,  XK_Q,        quit,                      { .i = 130             } },
        { MOD | ShiftMask,    XK_Q,        quit,                      { .i = 0               } },
        { MOD,                XK_Q,        closeActiveWindow,         {                      } },
        { MOD,                XK_J,        focus,                     { .i = +1              } },
        { MOD,                XK_K,        focus,                     { .i = -1              } },
        { MOD,                XK_D,        openApplication,           { .v = rofi            } },
        { MOD,                XK_Return,   openApplication,           { .v = alacritty       } },
        { MOD,                XK_F1,       openApplication,           { .v = chromium        } },
        { MOD,                XK_F12,      openApplication,           { .v = suspend,        } },
        { MOD,                XK_Z,        clearSplitHints,           {                      } },
        { MOD,                XK_X,        raiseSplit,                { .i = NONE            } },
        { MOD,                XK_C,        raiseSplit,                { .i = HORIZONTAL      } },
        { MOD,                XK_V,        raiseSplit,                { .i = VERTICAL        } },
        { MOD | ControlMask,  XK_X,        lowerSplit,                { .i = NONE            } },
        { MOD | ControlMask,  XK_C,        lowerSplit,                { .i = HORIZONTAL      } },
        { MOD | ControlMask,  XK_V,        lowerSplit,                { .i = VERTICAL        } },
        { MOD | ControlMask,  XK_H,        moveLeftEdgeHorizontally,  { .i = LEFT            } },
        { MOD | ControlMask,  XK_L,        moveLeftEdgeHorizontally,  { .i = RIGHT           } },
        { MOD | Mod1Mask,     XK_H,        moveRightEdgeHorizontally, { .i = LEFT            } },
        { MOD | Mod1Mask,     XK_L,        moveRightEdgeHorizontally, { .i = RIGHT           } },
        { MOD | ControlMask,  XK_K,        moveUpperEdgeVertically,   { .i = UP              } },
        { MOD | ControlMask,  XK_J,        moveUpperEdgeVertically,   { .i = DOWN            } },
        { MOD | Mod1Mask,     XK_K,        moveLowerEdgeVertically,   { .i = UP              } },
        { MOD | Mod1Mask,     XK_J,        moveLowerEdgeVertically,   { .i = DOWN            } },
        { MOD | ShiftMask,    XK_H,        moveNode,                  { .i = MOVE_LEFT       } },
        { MOD | ShiftMask,    XK_J,        moveNode,                  { .i = MOVE_DOWN       } },
        { MOD | ShiftMask,    XK_K,        moveNode,                  { .i = MOVE_UP         } },
        { MOD | ShiftMask,    XK_L,        moveNode,                  { .i = MOVE_RIGHT      } },
        { MOD,                XK_F,        toggleFullscreen,          {                      } },

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

#define RESIZE_DISTANCE 60

#endif
