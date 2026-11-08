#include "c_entity_archetype_manager.h"

namespace SDK {
    const EntityArchetypeMapNodeLayout *EntityArchetypeManagerLayout::FirstArchetype() const {
        if (mapHead == nullptr || archetypeCount == 0) {
            return nullptr;
        }

        // MSVC keeps the smallest key in the head sentinel's left link.
        const EntityArchetypeMapNodeLayout *node = mapHead->left;
        return node != nullptr && !node->IsNil() ? node : nullptr;
    }

    const EntityArchetypeMapNodeLayout *EntityArchetypeManagerLayout::NextArchetype(const EntityArchetypeMapNodeLayout *node) {
        if (node == nullptr || node->IsNil()) {
            return nullptr;
        }

        if (node->right != nullptr && !node->right->IsNil()) {
            const EntityArchetypeMapNodeLayout *successor = node->right;
            while (successor->left != nullptr && !successor->left->IsNil()) {
                successor = successor->left;
            }
            return successor;
        }

        // Climb until the node stops being a right child. The head sentinel
        // terminates the walk, which is what its nil flag is for.
        const EntityArchetypeMapNodeLayout *parent = node->parent;
        while (parent != nullptr && !parent->IsNil() && node == parent->right) {
            node   = parent;
            parent = node->parent;
        }

        return parent != nullptr && !parent->IsNil() ? parent : nullptr;
    }
} // namespace SDK
