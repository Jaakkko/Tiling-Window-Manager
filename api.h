//
// Created by jaakko on 10.4.2020.
//

#ifndef WM_API_H
#define WM_API_H

typedef union {
    int i;
    void* v;
} Arg;

// Keybindings
void closeActiveWindow(Arg);
void focus(Arg);
void openApplication(Arg);
void quit(Arg);
void selectWorkspace(Arg);
void moveToWorkspace(Arg);
void toggleToWorkspace(Arg);
void lowerSplit(Arg);
void raiseSplit(Arg);
void clearSplitHints(Arg);
void moveLeftEdgeHorizontally(Arg);
void moveRightEdgeHorizontally(Arg);
void moveUpperEdgeVertically(Arg);
void moveLowerEdgeVertically(Arg);

#endif //WM_API_H
