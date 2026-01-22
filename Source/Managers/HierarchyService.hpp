#pragma once

#include <optional>
#include <vector>

#include "../ECS/HierarchyTree.hpp"

namespace FeatherVK {
    class HierarchyService {
    public:
        HierarchyTree &GetTree() {
            return m_tree;
        }

        const HierarchyTree &GetTree() const {
            return m_tree;
        }

        void Reset() {
            m_tree.Reset();
        }

        void AttachEntity(id_t entityId, std::optional<id_t> parentEntityId) {
            m_tree.ReparentNode(parentEntityId, entityId);
        }

        void DetachEntity(id_t entityId) {
            m_tree.ReparentNode(std::nullopt, entityId);
        }

        void RemoveEntity(id_t entityId) {
            m_tree.RemoveNode(entityId);
        }

        void CollectSubtreeIds(id_t entityId, std::vector<id_t> &outIds) const {
            m_tree.CollectSubtreeIds(entityId, outIds);
        }

    private:
        HierarchyTree m_tree{};
    };
}
