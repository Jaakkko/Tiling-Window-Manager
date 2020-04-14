//
// Created by jaakko on 10.4.2020.
//

#include <X11/Xlib.h>

#include <stdlib.h>
#include <unistd.h>

#include "api.h"
#include "instance.h"

static wmWindow* nextVisibleWindow() {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    wmWindow* focus = activeWindow;
    if (focus) {
        unsigned mask = 1 << wmActiveWorkspace;
        while (1) {
            focus = focus->previous ? focus->previous : wmTail;
            if (!focus || focus == activeWindow) {
                return NULL;
            }
            if (focus->workspaces & mask) {
                break;
            }
        }
    }

    return focus;
}

void closeActiveWindow(Arg a) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        wmRequestCloseWindow(activeWindow);
    }
}

void focus(Arg a) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        wmWindow* focus = activeWindow;
        if (a.i == 1) {
            focus = nextVisibleWindow();
            if (focus) {
                wmFocusWindow(focus);
            }
        }
        else {
            unsigned mask = 1 << wmActiveWorkspace;
            while (1) {
                focus = focus->next ? focus->next : wmHead;
                if (!focus || focus == activeWindow) {
                    return;
                }
                if (focus->workspaces & mask) {
                    break;
                }
            }

            wmFocusWindow(focus);
        }
    }
}

void openApplication(Arg a) {
    if (fork() == 0) {
        setsid();
        char* cmd[] = { a.v, NULL };
        execvp(cmd[0], cmd);
        exit(EXIT_SUCCESS);
    }
}

void quit(Arg a) {
    wmRunning = False;
    wmExitCode = a.i;
}

void selectWorkspace(Arg a) {
    if (a.i != wmActiveWorkspace) {
        wmActiveWorkspace = a.i;
        wmShowActiveWorkspace();
    }
}

void moveToWorkspace(Arg a) {
    wmWindow** activeWindow = &wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (*activeWindow) {
        (*activeWindow)->workspaces = 1 << a.i;
        wmWorkspaces[a.i].activeWindow = *activeWindow;
        *activeWindow = nextVisibleWindow();
        wmShowActiveWorkspace();
    }
}
