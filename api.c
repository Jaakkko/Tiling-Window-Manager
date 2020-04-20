//
// Created by jaakko on 10.4.2020.
//

#include <X11/Xlib.h>

#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>

#include "api.h"
#include "instance.h"

void closeActiveWindow(Arg a) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        wmRequestCloseWindow(activeWindow);
    }
}

void focus(Arg a) {
    wmWindow* activeWindow = wmWorkspaces[wmActiveWorkspace].activeWindow;
    if (activeWindow) {
        wmWindow* focus =
                a.i == 1
                ? wmNextVisibleWindow(wmActiveWorkspace)
                : wmPreviousVisibleWindow(wmActiveWorkspace);
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
        wmSelectWorkspace(a.i);
    }
}

void moveToWorkspace(Arg a) {
    wmMoveActiveWindow(a.i);
}

void toggleToWorkspace(Arg a) {
    wmToggleActiveWindow(a.i);
}

void lowerSplit(Arg a) {
    wmLowerSplit(a.i);
}

void raiseSplit(Arg a) {
    wmRaiseSplit(a.i);
}

void clearSplitHints(Arg a) {
    wmWorkspaces[wmActiveWorkspace].showSplitBorder = 0;
    wmUpdateBorders();
}

void moveLeftEdgeHorizontally(Arg a) {
    wmMoveLeftEdgeHorizontally(a.i);
}

void moveRightEdgeHorizontally(Arg a) {
    wmMoveRightEdgeHorizontally(a.i);
}

void moveUpperEdgeVertically(Arg a) {
    wmMoveUpperEdgeVertically(a.i);
}

void moveLowerEdgeVertically(Arg a) {
    wmMoveLowerEdgeVertically(a.i);
}
