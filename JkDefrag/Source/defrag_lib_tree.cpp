#include "std_afx.h"

/* Return pointer to the first item in the tree (the first file on the volume). */
ItemStruct *DefragLib::tree_smallest(ItemStruct *top) {
    if (top == nullptr) return nullptr;

    while (top->smaller_ != nullptr) top = top->smaller_;

    return top;
}

/* Return pointer to the last item in the tree (the last file on the volume). */
ItemStruct *DefragLib::tree_biggest(ItemStruct *top) {
    if (top == nullptr) return nullptr;

    while (top->bigger_ != nullptr) top = top->bigger_;

    return top;
}

/*

If Direction=0 then return a pointer to the first file on the volume,
if Direction=1 then the last file.

*/
ItemStruct *DefragLib::tree_first(ItemStruct *top, const int direction) {
    if (direction == 0) return tree_smallest(top);

    return tree_biggest(top);
}

/* Return pointer to the previous item in the tree. */
ItemStruct *DefragLib::tree_prev(ItemStruct *here) {
    ItemStruct *temp;

    if (here == nullptr) return here;

    if (here->smaller_ != nullptr) {
        here = here->smaller_;

        while (here->bigger_ != nullptr) here = here->bigger_;

        return here;
    }

    do {
        temp = here;
        here = here->parent_;
    } while (here != nullptr && here->smaller_ == temp);

    return here;
}

/* Return pointer to the next item in the tree. */
ItemStruct *DefragLib::tree_next(ItemStruct *here) {
    ItemStruct *temp;

    if (here == nullptr) return nullptr;

    if (here->bigger_ != nullptr) {
        here = here->bigger_;

        while (here->smaller_ != nullptr) here = here->smaller_;

        return here;
    }

    do {
        temp = here;
        here = here->parent_;
    } while (here != nullptr && here->bigger_ == temp);

    return here;
}

/*

If Direction=0 then return a pointer to the next file on the volume,
if Direction=1 then the previous file.

*/
ItemStruct *DefragLib::tree_next_prev(ItemStruct *here, const bool reverse) {
    if (!reverse) return tree_next(here);

    return tree_prev(here);
}

/* Insert a record into the tree. The tree is sorted by LCN (Logical Cluster Number). */
void DefragLib::tree_insert(DefragDataStruct *data, ItemStruct *new_item) {
    ItemStruct *b;

    if (new_item == nullptr) return;

    const uint64_t new_lcn = get_item_lcn(new_item);

    /* Locate the place where the record should be inserted. */
    ItemStruct *here = data->item_tree_;
    ItemStruct *ins = nullptr;
    int found = 1;

    while (here != nullptr) {
        ins = here;
        found = 0;

        if (const uint64_t here_lcn = get_item_lcn(here); here_lcn > new_lcn) {
            found = 1;
            here = here->smaller_;
        } else {
            if (here_lcn < new_lcn) found = -1;

            here = here->bigger_;
        }
    }

    /* Insert the record. */
    new_item->parent_ = ins;
    new_item->smaller_ = nullptr;
    new_item->bigger_ = nullptr;

    if (ins == nullptr) {
        data->item_tree_ = new_item;
    } else {
        if (found > 0) {
            ins->smaller_ = new_item;
        } else {
            ins->bigger_ = new_item;
        }
    }

    /* If there have been less than 1000 inserts then return. */
    data->balance_count_ = data->balance_count_ + 1;

    if (data->balance_count_ < 1000) return;

    /* Balance the tree.
    It's difficult to explain what exactly happens here. For an excellent
    tutorial see:
    http://www.stanford.edu/~blp/avl/libavl.html/Balancing-a-BST.html
    */

    data->balance_count_ = 0;

    /* Convert the tree into a vine. */
    ItemStruct *a = data->item_tree_;
    ItemStruct *c = a;
    long count = 0;

    while (a != nullptr) {
        /* If A has no Bigger child then move down the tree. */
        if (a->bigger_ == nullptr) {
            count = count + 1;
            c = a;
            a = a->smaller_;

            continue;
        }

        /* Rotate left at A. */
        b = a->bigger_;

        if (data->item_tree_ == a) data->item_tree_ = b;

        a->bigger_ = b->smaller_;

        if (a->bigger_ != nullptr) a->bigger_->parent_ = a;

        b->parent_ = a->parent_;

        if (b->parent_ != nullptr) {
            if (b->parent_->smaller_ == a) {
                b->parent_->smaller_ = b;
            } else {
                a->parent_->bigger_ = b;
            }
        }

        b->smaller_ = a;
        a->parent_ = b;

        /* Do again. */
        a = b;
    }

    /* Calculate the number of skips. */
    long skip = 1;

    while (skip < count + 2) skip = skip << 1;

    skip = count + 1 - (skip >> 1);

    /* Compress the tree. */
    while (c != nullptr) {
        if (skip <= 0) c = c->parent_;

        a = c;

        while (a != nullptr) {
            b = a;
            a = a->parent_;

            if (a == nullptr) break;

            /* Rotate right at A. */
            if (data->item_tree_ == a) data->item_tree_ = b;

            a->smaller_ = b->bigger_;

            if (a->smaller_ != nullptr) a->smaller_->parent_ = a;

            b->parent_ = a->parent_;

            if (b->parent_ != nullptr) {
                if (b->parent_->smaller_ == a) {
                    b->parent_->smaller_ = b;
                } else {
                    b->parent_->bigger_ = b;
                }
            }

            a->parent_ = b;
            b->bigger_ = a;

            /* Next item. */
            a = b->parent_;

            /* If there were skips then leave if all done. */
            skip = skip - 1;
            if (skip == 0) break;
        }
    }
}

/*

Detach (unlink) a record from the tree. The record is not freed().
See: http://www.stanford.edu/~blp/avl/libavl.html/Deleting-from-a-BST.html

*/
void DefragLib::tree_detach(DefragDataStruct *data, const ItemStruct *item) {
    /* Sanity check. */
    if (data->item_tree_ == nullptr || item == nullptr) return;

    if (item->bigger_ == nullptr) {
        /* It is trivial to delete a node with no Bigger child. We replace
        the pointer leading to the node by it's Smaller child. In
        other words, we replace the deleted node by its Smaller child. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = item->smaller_;
            } else {
                item->parent_->bigger_ = item->smaller_;
            }
        } else {
            data->item_tree_ = item->smaller_;
        }

        if (item->smaller_ != nullptr) item->smaller_->parent_ = item->parent_;
    } else if (item->bigger_->smaller_ == nullptr) {
        /* The Bigger child has no Smaller child. In this case, we move Bigger
        into the node's place, attaching the node's Smaller subtree as the
        new Smaller. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = item->bigger_;
            } else {
                item->parent_->bigger_ = item->bigger_;
            }
        } else {
            data->item_tree_ = item->bigger_;
        }

        item->bigger_->parent_ = item->parent_;
        item->bigger_->smaller_ = item->smaller_;

        if (item->smaller_ != nullptr) item->smaller_->parent_ = item->bigger_;
    } else {
        /* Replace the node by it's inorder successor, that is, the node with
        the smallest value greater than the node. We know it exists because
        otherwise this would be case 1 or case 2, and it cannot have a Smaller
        value because that would be the node itself. The successor can
        therefore be detached and can be used to replace the node. */

        /* Find the inorder successor. */
        ItemStruct *b = item->bigger_;
        while (b->smaller_ != nullptr) b = b->smaller_;

        /* Detach the successor. */
        if (b->parent_ != nullptr) {
            if (b->parent_->bigger_ == b) {
                b->parent_->bigger_ = b->bigger_;
            } else {
                b->parent_->smaller_ = b->bigger_;
            }
        }

        if (b->bigger_ != nullptr) b->bigger_->parent_ = b->parent_;

        /* Replace the node with the successor. */
        if (item->parent_ != nullptr) {
            if (item->parent_->smaller_ == item) {
                item->parent_->smaller_ = b;
            } else {
                item->parent_->bigger_ = b;
            }
        } else {
            data->item_tree_ = b;
        }

        b->parent_ = item->parent_;
        b->smaller_ = item->smaller_;

        if (b->smaller_ != nullptr) b->smaller_->parent_ = b;

        b->bigger_ = item->bigger_;

        if (b->bigger_ != nullptr) b->bigger_->parent_ = b;
    }
}

/* Delete the entire ItemTree.
 * TODO: top is owning pointer, to be freed
 * */
void DefragLib::delete_item_tree(ItemStruct *top) {
    if (top == nullptr) return;
    if (top->smaller_ != nullptr) delete_item_tree(top->smaller_);
    if (top->bigger_ != nullptr) delete_item_tree(top->bigger_);

    while (top->fragments_ != nullptr) {
        FragmentListStruct *fragment = top->fragments_->next_;
        delete top->fragments_;

        top->fragments_ = fragment;
    }

    delete top;
}
