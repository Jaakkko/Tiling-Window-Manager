//
// Created by jaakko on 27.4.2020.
//

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "tree.h"
#include "logger.h"

inline wmNode newNode(wmWindow* window) {
    wmNode node;
    node.numChildren = 0;
    node.nodes = NULL;
    node.window = window;
    node.orientation = NONE;
    node.weight = 1.0f;
    node.x = 0;
    node.y = 0;
    node.width = 0;
    node.height = 0;
    return node;
}

wmNode* addNode(wmNode* parent, wmNode child, wmSplitMode splitOrientation) {
    if (splitOrientation != NONE || parent->window) {
        if (splitOrientation == NONE) {
            splitOrientation = parent->width > parent->height ? HORIZONTAL : VERTICAL;
        }

        wmNode oldNode = *parent;
        parent->orientation = splitOrientation;
        parent->nodes = calloc(2, sizeof(wmNode));
        parent->window = NULL;
        parent->numChildren = 2;

        parent->nodes[0] = oldNode;
        parent->nodes[1] = child;
        parent->nodes[0].weight = 0.5;
        parent->nodes[1].weight = 0.5;

        if (splitOrientation == VERTICAL) {
            wmNode* node = parent->nodes;
            node->width = 1;
            node->height = 0;
            node++;
            node->width = 1;
            node->height = 0;
        }
        else {
            wmNode* node = parent->nodes;
            node->width = 0;
            node->height = 1;
            node++;
            node->width = 0;
            node->height = 1;
        }

        return &parent->nodes[1];
    }

    if (parent->nodes) {
        parent->numChildren++;
        parent->nodes = realloc(parent->nodes, parent->numChildren * sizeof(wmNode));

        int index = parent->numChildren - 1;
        parent->nodes[index] = child;

        float weight = 1.0f / parent->numChildren;
        for (int i = 0; i < parent->numChildren; i++) {
            parent->nodes[i].weight = weight;
        }

        return &parent->nodes[index];
    }

    logmsg("addNode(...) invalid parent.");
    return NULL;
}

wmNode* removeNode(wmNode* layout, wmNode* node) {
    for (int i = 0; i < layout->numChildren; i++) {
        wmNode* child = layout->nodes + i;
        if (child == node) {
            layout->numChildren--;
            if (layout->numChildren == 1) {
                wmNode lastOne = layout->nodes[1 ^ i];
                lastOne.weight = layout->weight;
                free(layout->nodes);
                *layout = lastOne;
            }
            else {
                memmove(child, child + 1, (layout->numChildren - i) * sizeof(wmNode));
                layout->nodes = realloc(layout->nodes, layout->numChildren * sizeof(wmNode));

                float weight = 1.0f / layout->numChildren;
                for (int j = 0; j < layout->numChildren; j++) {
                    layout->nodes[j].weight = weight;
                }
            }

            return layout;
        }

        wmNode* found = removeNode(child, node);
        if (found) {
            return found;
        }
    }

    return NULL;
}

wmNode* findNode(wmNode* layout, wmWindow* window) {
    if (layout->window == window) {
        return layout;
    }

    for (int i = 0; i < layout->numChildren; i++) {
        wmNode* found = findNode(layout->nodes + i, window);
        if (found) {
            return found;
        }
    }

    return NULL;
}

wmNode* findParent(wmNode* layout, wmNode* child) {
    for (int i = 0; i < layout->numChildren; i++) {
        wmNode* ptr = layout->nodes + i;
        if (ptr == child) {
            return layout;
        }

        wmNode* found = findParent(ptr, child);
        if (found) {
            return found;
        }
    }

    return NULL;
}

int indexOf(wmNode* layout, wmNode* child, wmNode** parent, int* index) {
    for (int i = 0; i < layout->numChildren; i++) {
        wmNode* ptr = layout->nodes + i;
        if (ptr == child) {
            *parent = layout;
            *index = i;
            return 1;
        }

        if (indexOf(ptr, child, parent, index)) {
            return 1;
        }
    }

    return 0;
}

inline void swap(wmNode* parent, int aIndex, int bIndex) {
    wmNode tmp = parent->nodes[aIndex];
    parent->nodes[aIndex] = parent->nodes[bIndex];
    parent->nodes[bIndex] = tmp;
}

static wmNode* raise(wmNode* layout, wmNode* parent, int index, int indexRising, wmSplitMode orientation) {
    parent->numChildren--;

    float weight;
    if (layout == parent) {
        if (parent->numChildren == 1) {
            parent->numChildren = 2;
            parent->orientation = orientation;
            if (index != indexRising) {
                swap(parent, 0, 1);
            }
            return &parent->nodes[indexRising];
        }

        wmNode a = parent->nodes[index];

        wmNode* dst = parent->nodes + index;
        memmove(dst, dst + 1, sizeof(wmNode) * (parent->numChildren - index));
        parent->nodes = realloc(parent->nodes, sizeof(wmNode) * parent->numChildren);
        wmNode b = *parent;
        weight = 1.0f / b.numChildren;
        for (int i = 0; i < b.numChildren; i++) {
            b.nodes[i].weight = weight;
        }

        parent->nodes = calloc(2, sizeof(wmNode));
        parent->nodes[0] = a;
        parent->nodes[0].weight = 0.5f;
        parent->nodes[1] = b;
        parent->nodes[1].weight = 0.5f;
        parent->orientation = orientation;
        if (indexRising) {
            swap(parent, 0, 1);
        }
        return &parent->nodes[indexRising];
    }

    wmNode new[2];
    int n;

    if (parent->numChildren == 1) {
        n = 2;
        if (index) {
            swap(parent, 0, 1);
        }
        memcpy(new, parent->nodes, sizeof(wmNode) * 2);
        free(parent->nodes);
        parent->numChildren = 0;
    }
    else {
        n = 1;
        memcpy(new, parent->nodes + index, sizeof(wmNode));

        // Remove moving
        wmNode* dst = parent->nodes + index;
        memmove(dst, dst + 1, sizeof(wmNode) * (parent->numChildren - index));
        parent->nodes = realloc(parent->nodes, sizeof(wmNode) * parent->numChildren);
        weight = 1.0f / parent->numChildren;
        for (int i = 0; i < parent->numChildren; i++) {
            parent->nodes[i].weight = weight;
        }
    }

    // Find destination
    wmNode* destParent;
    int destIndex;
    indexOf(layout, parent, &destParent, &destIndex);

    // Expand array + insert
    destParent->numChildren++;
    destParent->nodes = realloc(destParent->nodes, sizeof(wmNode) * destParent->numChildren);
    wmNode* src = destParent->nodes + destIndex;
    memmove(src + 1, src, sizeof(wmNode) * (destParent->numChildren - 1 - destIndex));
    memcpy(src, new, sizeof(wmNode) * n);
    if (indexRising) {
        swap(destParent, destIndex, destIndex + 1);
    }
    weight = 1.0f / destParent->numChildren;
    for (int i = 0; i < destParent->numChildren; i++) {
        destParent->nodes[i].weight = weight;
    }

    return indexRising ? src + 1 : src;
}
wmNode* raiseLeft(wmNode* layout, wmNode* parent, int index) {
    return raise(layout, parent, index, 0, HORIZONTAL);
}
wmNode* raiseRight(wmNode* layout, wmNode* parent, int index) {
    return raise(layout, parent, index, 1, HORIZONTAL);
}
wmNode* raiseUp(wmNode* layout, wmNode* parent, int index) {
    return raise(layout, parent, index, 0, VERTICAL);
}
wmNode* raiseDown(wmNode* layout, wmNode* parent, int index) {
    return raise(layout, parent, index, 1, VERTICAL);
}

void freeTree(wmNode* layout) {
    for (int i = 0; i < layout->numChildren; i++) {
        freeTree(layout->nodes + i);
    }

    free(layout->nodes);
    layout->nodes = NULL;
    layout->numChildren = 0;
}
