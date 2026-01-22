#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <unordered_map>
#include <vector>

#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class HierarchyTree {
    public:
        inline static constexpr id_t RootId = std::numeric_limits<id_t>::max();
        inline static constexpr id_t ROOT_ID = RootId;

        struct Node {
            id_t id = RootId;
            id_t parentId = RootId;
            std::vector<id_t> children{};
        };

        const Node &GetRoot() const {
            return m_root;
        }

        const std::vector<id_t> &GetRootChildren() const {
            return m_root.children;
        }

        bool AddNode(std::optional<id_t> parentId, id_t childId) {
            return ReparentNode(parentId, childId);
        }

        bool ReparentNode(std::optional<id_t> parentId, id_t childId) {
            if (childId == RootId) {
                return false;
            }

            const id_t normalizedParentId = NormalizeParent(parentId, childId);
            DetachNode(childId);

            if (normalizedParentId != RootId) {
                auto &parentNode = m_nodes[normalizedParentId];
                parentNode.id = normalizedParentId;
            }

            auto &childNode = m_nodes[childId];
            childNode.id = childId;
            childNode.parentId = normalizedParentId;

            auto &siblings = normalizedParentId == RootId ? m_root.children : m_nodes[normalizedParentId].children;
            if (std::find(siblings.begin(), siblings.end(), childId) == siblings.end()) {
                siblings.push_back(childId);
            }

            return true;
        }

        Node *FindNode(id_t id) {
            const auto it = m_nodes.find(id);
            return it == m_nodes.end() ? nullptr : &it->second;
        }

        const Node *FindNode(id_t id) const {
            const auto it = m_nodes.find(id);
            return it == m_nodes.end() ? nullptr : &it->second;
        }

        const std::vector<id_t> &GetChildren(id_t parentId) const {
            if (parentId == RootId) {
                return m_root.children;
            }

            const auto it = m_nodes.find(parentId);
            return it == m_nodes.end() ? m_emptyChildren : it->second.children;
        }

        bool RemoveNode(id_t id) {
            auto nodeIt = m_nodes.find(id);
            if (nodeIt == m_nodes.end()) {
                return false;
            }

            const std::vector<id_t> children = nodeIt->second.children;
            for (const id_t childId: children) {
                RemoveNode(childId);
            }

            DetachNode(id);
            m_nodes.erase(id);
            return true;
        }

        void CollectSubtreeIds(id_t id, std::vector<id_t> &outIds) const {
            const Node *node = FindNode(id);
            if (node == nullptr) {
                return;
            }
            CollectSubtreeIds(*node, outIds);
        }

        void Reset() {
            m_nodes.clear();
            m_root = Node{};
        }

    private:
        void CollectSubtreeIds(const Node &node, std::vector<id_t> &outIds) const {
            for (const id_t childId: node.children) {
                const auto childIt = m_nodes.find(childId);
                if (childIt != m_nodes.end()) {
                    CollectSubtreeIds(childIt->second, outIds);
                }
            }
            outIds.push_back(node.id);
        }

        id_t NormalizeParent(std::optional<id_t> parentId, id_t childId) const {
            if (!parentId.has_value() || *parentId == RootId || *parentId == childId) {
                return RootId;
            }

            if (WouldCreateCycle(*parentId, childId)) {
                return RootId;
            }

            return *parentId;
        }

        bool WouldCreateCycle(id_t parentId, id_t childId) const {
            id_t cursor = parentId;
            while (cursor != RootId) {
                if (cursor == childId) {
                    return true;
                }

                const auto it = m_nodes.find(cursor);
                if (it == m_nodes.end()) {
                    return false;
                }
                cursor = it->second.parentId;
            }
            return false;
        }

        void DetachNode(id_t id) {
            const auto nodeIt = m_nodes.find(id);
            if (nodeIt == m_nodes.end()) {
                return;
            }

            auto &siblings = nodeIt->second.parentId == RootId ? m_root.children : m_nodes[nodeIt->second.parentId].children;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
        }

        Node m_root{};
        std::unordered_map<id_t, Node> m_nodes{};
        std::vector<id_t> m_emptyChildren{};
    };
}
