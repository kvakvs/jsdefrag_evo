#pragma once

#include <cstdint>

namespace Tree {

    // Return pointer to the first item in the tree (the first file on the volume)
    template<class NODE>
    NODE *smallest(NODE *top) {
        if (top == nullptr) return nullptr;
        while (top->smaller_ != nullptr) top = top->smaller_;
        return top;
    }

    // Return pointer to the last item in the tree (the last file on the volume)
    template<class NODE>
    NODE *biggest(NODE *top) {
        if (top == nullptr) return nullptr;
        while (top->bigger_ != nullptr) top = top->bigger_;
        return top;
    }

    enum Direction {
        First,
        Last,
    };

    // If Direction=0 then return a pointer to the first file on the volume,
    // if Direction=1 then the last file.
    template<class NODE>
    NODE *first(NODE *top, Direction direction) {
        return direction == First ? smallest(top) : biggest(top);
    }

    // Return pointer to the previous item in the tree
    template<class NODE>
    NODE *prev(NODE *here) {
        if (here == nullptr) return here;

        if (here->smaller_ != nullptr) {
            here = here->smaller_;

            while (here->bigger_ != nullptr) here = here->bigger_;

            return here;
        }

        NODE *temp;

        do {
            temp = here;
            here = here->parent_;
        } while (here != nullptr && here->smaller_ == temp);

        return here;
    }

    // Return pointer to the next item in the tree
    template<class NODE>
    NODE *next(NODE *here) {
        if (here == nullptr) return nullptr;

        if (here->bigger_ != nullptr) {
            here = here->bigger_;

            while (here->smaller_ != nullptr) here = here->smaller_;

            return here;
        }

        NODE *temp;

        do {
            temp = here;
            here = here->parent_;
        } while (here != nullptr && here->bigger_ == temp);

        return here;
    }

    enum StepDirection {
        StepForward,
        StepBack,
    };

    // If StepDirection=0 then return a pointer to the next file on the volume,
    // if StepDirection=1 then the previous file.
    template<class NODE>
    NODE *next_prev(NODE *here, const StepDirection step_direction) {
        return step_direction == StepForward ? next(here) : prev(here);
    }

    // Insert a record into the tree. The tree is sorted by LCN (Logical Cluster Number)
    template<class NODE>
    void insert(NODE *&root, int &balance_count, NODE *new_item) {
        if (new_item == nullptr) return;

        const auto new_lcn = new_item->get_item_lcn();

        // Locate the place where the record should be inserted
        NODE *here = root;
        NODE *ins = nullptr;
        int found = 1;

        while (here != nullptr) {
            ins = here;
            found = 0;

            if (const Clusters64 here_lcn = here->get_item_lcn(); here_lcn > new_lcn) {
                found = 1;
                here = here->smaller_;
            } else {
                if (here_lcn < new_lcn) found = -1;

                here = here->bigger_;
            }
        }

        // Insert the record
        new_item->parent_ = ins;
        new_item->smaller_ = nullptr;
        new_item->bigger_ = nullptr;

        if (ins == nullptr) {
            root = new_item;
        } else {
            if (found > 0) {
                ins->smaller_ = new_item;
            } else {
                ins->bigger_ = new_item;
            }
        }

        // If there have been less than 1000 inserts then return
        balance_count++;

        if (balance_count < 1000) return;

        /* Balance the tree.
        It's difficult to explain what exactly happens here. For an excellent
        tutorial see:
        http://www.stanford.edu/~blp/avl/libavl.html/Balancing-a-BST.html
        */

        balance_count = 0;

        // Convert the tree into a vine
        NODE *a = root;
        NODE *b;
        NODE *c = a;
        long count = 0;

        while (a != nullptr) {
            // If A has no Bigger child then move down the tree
            if (a->bigger_ == nullptr) {
                count = count + 1;
                c = a;
                a = a->smaller_;

                continue;
            }

            // Rotate left at A
            b = a->bigger_;

            if (root == a) root = b;

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

            // Do again
            a = b;
        }

        // Calculate the number of skips
        long skip = 1;

        while (skip < count + 2) skip = skip << 1;

        skip = count + 1 - (skip >> 1);

        // Compress the tree
        while (c != nullptr) {
            if (skip <= 0) c = c->parent_;

            a = c;

            while (a != nullptr) {
                b = a;
                a = a->parent_;

                if (a == nullptr) break;

                // Rotate right at A
                if (root == a) root = b;

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

                // Next item
                a = b->parent_;

                // If there were skips then leave if all done
                skip = skip - 1;
                if (skip == 0) break;
            }
        }
    }

    // Detach (unlink) a record from the tree. The record is not freed().
    // See: http://www.stanford.edu/~blp/avl/libavl.html/Deleting-from-a-BST.html
    template<class NODE>
    void detach(NODE *&root, const NODE *item) {
        // Sanity check
        if (root == nullptr || item == nullptr) return;

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
                root = item->smaller_;
            }

            if (item->smaller_ != nullptr) item->smaller_->parent_ = item->parent_;
        } else if (item->bigger_->smaller_ == nullptr) {
            // The Bigger child has no Smaller child. In this case, we move Bigger
            // into the node's place, attaching the node's Smaller subtree as the
            // new Smaller.
            if (item->parent_ != nullptr) {
                if (item->parent_->smaller_ == item) {
                    item->parent_->smaller_ = item->bigger_;
                } else {
                    item->parent_->bigger_ = item->bigger_;
                }
            } else {
                root = item->bigger_;
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

            // Find the inorder successor
            NODE *b = item->bigger_;
            while (b->smaller_ != nullptr) b = b->smaller_;

            // Detach the successor
            if (b->parent_ != nullptr) {
                if (b->parent_->bigger_ == b) {
                    b->parent_->bigger_ = b->bigger_;
                } else {
                    b->parent_->smaller_ = b->bigger_;
                }
            }

            if (b->bigger_ != nullptr) b->bigger_->parent_ = b->parent_;

            // Replace the node with the successor
            if (item->parent_ != nullptr) {
                if (item->parent_->smaller_ == item) {
                    item->parent_->smaller_ = b;
                } else {
                    item->parent_->bigger_ = b;
                }
            } else {
                root = b;
            }

            b->parent_ = item->parent_;
            b->smaller_ = item->smaller_;

            if (b->smaller_ != nullptr) b->smaller_->parent_ = b;

            b->bigger_ = item->bigger_;

            if (b->bigger_ != nullptr) b->bigger_->parent_ = b;
        }
    }

    template<class NODE>
    void delete_node_rec(NODE *top) {
        if (top == nullptr) return;
        delete_node_rec(top->smaller_);
        delete_node_rec(top->bigger_);
        delete top;
    }

    template<class NODE>
    void delete_tree(NODE *&root) {
        delete_node_rec(root);
        root = nullptr;
    }
}
