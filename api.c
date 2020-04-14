//
// Created by jaakko on 10.4.2020.
//

#include <X11/Xlib.h>

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#include "api.h"
#include "instance.h"

static wmWindow* visibleWindow(size_t offset, wmWindow* head) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        wmWindow* focus = activeWindow;
        unsigned mask = 1 << wmActiveWorkspace;
        int i = 0;
        while (i++ < 10) {
            focus = *(wmWindow**)((char*)focus + offset);
            if (!focus) {
                focus = head;
            }
            if (!focus || focus == activeWindow) {
                return NULL;
            }
            if (focus->workspaces & mask) {
                return focus;
            }
        }
    }

    return NULL;
}

static wmWindow* nextVisibleWindow() {
    return visibleWindow(offsetof(wmWindow, next), wmHead);
}

static wmWindow* previousVisibleWindow() {
    return visibleWindow(offsetof(wmWindow, previous), wmTail);
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
        wmWindow* focus = a.i == 1 ? nextVisibleWindow() : previousVisibleWindow();
        if (focus) {
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
