//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_INSTANCE_H
#define WM_INSTANCE_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "types.h"

extern int wmMouseX;
extern int wmMouseY;

extern unsigned wmActiveWorkspace;

typedef enum { RIGHT, LEFT } wmHorizontalDirection;
typedef enum { DOWN, UP } wmVerticalDirection;

extern wmWorkspace wmWorkspaces[];

extern int wmRunning;
extern int wmExitCode;

extern Cursor wmCursors[];

extern Display* wmDisplay;
extern Window wmRoot;
extern int wmDepth;
extern Visual* wmVisual;
extern Colormap wmColormap;
extern int wmScreenWidth;
extern int wmScreenHeight;

extern int skipNextEnterNotify;

#define _NET_WM_STATE_SUPPORTED_COUNT 3

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */

typedef struct {
    Atom* atom;
    void (*handler)(XClientMessageEvent*);
} ClientMessageHandler;
extern const unsigned clientMessageHandlersCount;
extern ClientMessageHandler clientMessageHandler[];

int wmInitialize();
void wmRun();
void wmFree();

wmWindow* wmNextVisibleWindow(unsigned workspace);
wmWindow* wmPreviousVisibleWindow(unsigned workspace);
void wmMoveActiveWindow(unsigned workspace);
void wmToggleActiveWindow(unsigned workspaceIndex);

void wmFocusWindow(wmWindow* window);
void wmRequestCloseWindow(wmWindow* window);

void wmNewWindow(Window window, const XWindowAttributes* attributes);
void wmFreeWindow(wmWindow* window);
wmWindow* wmWindowTowmWindow(Window window);

void wmSelectWorkspace(unsigned workspaceIndex);
void wmShowActiveWorkspace();

void wmLowerSplit(wmSplitMode orientation);
void wmRaiseSplit(wmSplitMode orientation);
void wmUpdateBorders();

void wmMoveLeftEdgeHorizontally(wmHorizontalDirection direction);
void wmMoveRightEdgeHorizontally(wmHorizontalDirection direction);
void wmMoveUpperEdgeVertically(wmVerticalDirection direction);
void wmMoveLowerEdgeVertically(wmVerticalDirection direction);

void wmConfigureWindow(wmWindow* window);
void wmUpdateBounds();

void wmMoveNode(wmMoveDirection direction);

void wmUpdateMouseCoords();

void wmToggleFullscreen();

#endif //WM_INSTANCE_H
