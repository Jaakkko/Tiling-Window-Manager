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

void swap(wmNode* parent, int aIndex, int bIndex) {
    wmNode tmp = parent->nodes[aIndex];
    parent->nodes[aIndex] = parent->nodes[bIndex];
    parent->nodes[bIndex] = tmp;
}

void freeTree(wmNode* layout) {
    for (int i = 0; i < layout->numChildren; i++) {
        freeTree(layout->nodes + i);
    }

    free(layout->nodes);
    layout->nodes = NULL;
    layout->numChildren = 0;
}
