//
// Created by jaakko on 27.4.2020.
//

#ifndef WM_TYPES_H
#define WM_TYPES_H

typedef struct wmWindow wmWindow;
typedef struct wmNode wmNode;
typedef struct wmFloatingWindow wmFloatingWindow;

typedef struct {
    wmWindow* activeWindow;
    wmNode* layout;
    wmNode* splitNode;
    unsigned showSplitBorder;
} wmWorkspace;

#define FLOATING_DIALOG 1 << 1
#define FLOATING_STICKY 1 << 2

struct wmFloatingWindow {
    wmWindow* window;
    int x;
    int y;
    unsigned width;
    unsigned height;
    int flags;
    wmFloatingWindow* next;
};

typedef enum { HORIZONTAL, VERTICAL, NONE } wmSplitMode;

struct wmWindow {
    wmWindow* next;
    wmWindow* previous;
    Window window;
    Window frame;
    unsigned workspaces;
    wmFloatingWindow* floating;
};

struct wmNode {
    unsigned numChildren;
    wmNode* nodes;
    wmWindow* window;
    wmSplitMode orientation;
    float weight;
    int x;
    int y;
    unsigned width;
    unsigned height;
};

typedef enum { MOVE_LEFT, MOVE_UP, MOVE_RIGHT, MOVE_DOWN } wmMoveDirection;

typedef enum {
    CURSOR_DEFAULT,
    CURSOR_DRAG,
    CURSOR_RESIZE_TOP_LEFT,
    CURSOR_RESIZE_TOP,
    CURSOR_RESIZE_TOP_RIGHT,
    CURSOR_RESIZE_RIGHT,
    CURSOR_RESIZE_BOTTOM_RIGHT,
    CURSOR_RESIZE_BOTTOM,
    CURSOR_RESIZE_BOTTOM_LEFT,
    CURSOR_RESIZE_LEFT,
    CURSOR_LAST
} wmCursorTypes;

#endif //WM_TYPES_H
