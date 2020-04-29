//
// Created by jaakko on 9.4.2020.
//

#ifndef WM_INSTANCE_H
#define WM_INSTANCE_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "types.h"

int wmSkipNextEnterNotify;
int wmMouseX;
int wmMouseY;

unsigned wmActiveWorkspace;

wmDialog* wmDialogs;

wmWindow* wmHead;
wmWindow* wmTail;

typedef enum { RIGHT, LEFT } wmHorizontalDirection;
typedef enum { DOWN, UP } wmVerticalDirection;

wmSplitMode wmSplitOrientation;

extern wmWorkspace wmWorkspaces[];

int wmRunning;
int wmExitCode;

Cursor wmCursor;

Display* wmDisplay;
Window wmRoot;
int wmDepth;
Visual* wmVisual;
Colormap wmColormap;
int wmScreenWidth;
int wmScreenHeight;
int wmWindowAreaX;
int wmWindowAreaY;
int wmWindowAreaWidth;
int wmWindowAreaHeight;

Atom
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    WM_STATE,
    WM_TAKE_FOCUS,
    _NET_SUPPORTED,
    _NET_CLIENT_LIST,
    _NET_ACTIVE_WINDOW,
    _NET_SUPPORTING_WM_CHECK,
    _NET_REQUEST_FRAME_EXTENTS,
    _NET_FRAME_EXTENTS,
    _NET_WM_NAME,
    _NET_WM_STATE,
    _NET_WM_STATE_FULLSCREEN;

typedef struct {
    Atom* atom;
    void (*handler)(XClientMessageEvent*);
} ClientMessageHandler;
const unsigned clientMessageHandlersCount;
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

void wmMoveNode(wmMoveDirection direction);

#endif //WM_INSTANCE_H
