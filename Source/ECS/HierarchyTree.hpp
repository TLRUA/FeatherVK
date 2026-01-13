#pragma once

#include <algorithm>
#include <vector>

namespace FeatherVK {
    class HierarchyTree {
    public:
        struct Node {
            int id;
            int parentTransformId = -2;
            std::vector<Node *> children;
        };

        static const int ROOT_ID = -1;
        static const int DEFAULT_TRANSFORM_ID = -2;

        HierarchyTree() { m_root = new Node{ROOT_ID}; }

        ~HierarchyTree() { DeleteSubtree(m_root); }

        Node *GetRoot() {
            return m_root;
        }

        const Node *GetRoot() const {
            return m_root;
        }

        bool AddNode(int parentId, int childId, int childTransformId = DEFAULT_TRANSFORM_ID) {
            auto *parentNode = FindNode(parentId, m_root);

            if (parentNode == nullptr) {
                auto *node = new Node{childId, parentId};
                m_fakeNodes.push_back(node);
                m_root->children.push_back(node);
            } else {
                auto *node = new Node{childId};
                parentNode->children.push_back(node);
                for (auto fakeIt = m_fakeNodes.begin(); fakeIt != m_fakeNodes.end();) {
                    Node *fakeNode = *fakeIt;
                    if (childId == fakeNode->parentTransformId) {
                        node->children.push_back(fakeNode);
                        auto rootChildIt = std::find(m_root->children.begin(), m_root->children.end(), fakeNode);
                        if (rootChildIt != m_root->children.end()) {
                            m_root->children.erase(rootChildIt);
                        }

                        fakeIt = m_fakeNodes.erase(fakeIt);
                    } else {
                        ++fakeIt;
                    }
                }
            }

            return true;
        }

        Node *FindNode(int id) {
            return FindNode(id, m_root);
        }

        const Node *FindNode(int id) const {
            return FindNode(id, m_root);
        }

        bool RemoveNode(int id) {
            if (id == ROOT_ID || m_root == nullptr) {
                return false;
            }
            return RemoveNodeInternal(m_root, id);
        }

        void CollectSubtreeIds(int id, std::vector<int> &outIds) const {
            const Node *node = FindNode(id, m_root);
            if (node == nullptr) {
                return;
            }
            CollectSubtreeIds(node, outIds);
        }

        void Reset() {
            DeleteSubtree(m_root);
            m_root = new Node{ROOT_ID};
            m_fakeNodes.clear();
        }

    private:
        Node *m_root = nullptr;
        std::vector<Node *> m_fakeNodes;

        Node *FindNode(int id, Node *tmpNode) {
            if (tmpNode->id == id) {
                return tmpNode;
            }
            for (auto &child: tmpNode->children) {
                auto *node = FindNode(id, child);
                if (node != nullptr) {
                    return node;
                }
            }
            return nullptr;
        }

        const Node *FindNode(int id, const Node *tmpNode) const {
            if (tmpNode == nullptr) {
                return nullptr;
            }
            if (tmpNode->id == id) {
                return tmpNode;
            }
            for (const auto *child: tmpNode->children) {
                const Node *node = FindNode(id, child);
                if (node != nullptr) {
                    return node;
                }
            }
            return nullptr;
        }

        static void CollectSubtreeIds(const Node *node, std::vector<int> &outIds) {
            if (node == nullptr) {
                return;
            }
            for (const Node *child: node->children) {
                CollectSubtreeIds(child, outIds);
            }
            outIds.push_back(node->id);
        }

        bool RemoveNodeInternal(Node *parent, int id) {
            if (parent == nullptr) {
                return false;
            }

            for (auto it = parent->children.begin(); it != parent->children.end(); ++it) {
                Node *child = *it;
                if (child == nullptr) {
                    continue;
                }
                if (child->id == id) {
                    std::vector<Node *> subtreeNodes{};
                    CollectSubtreeNodes(child, subtreeNodes);
                    m_fakeNodes.erase(
                            std::remove_if(
                                    m_fakeNodes.begin(),
                                    m_fakeNodes.end(),
                                    [&subtreeNodes](Node *fakeNode) {
                                        return std::find(subtreeNodes.begin(), subtreeNodes.end(), fakeNode) != subtreeNodes.end();
                                    }),
                            m_fakeNodes.end());
                    DeleteSubtree(child);
                    parent->children.erase(it);
                    return true;
                }
                if (RemoveNodeInternal(child, id)) {
                    return true;
                }
            }
            return false;
        }

        static void DeleteSubtree(Node *node) {
            if (node == nullptr) {
                return;
            }
            for (Node *child: node->children) {
                DeleteSubtree(child);
            }
            delete node;
        }

        static void CollectSubtreeNodes(Node *node, std::vector<Node *> &outNodes) {
            if (node == nullptr) {
                return;
            }
            outNodes.push_back(node);
            for (Node *child: node->children) {
                CollectSubtreeNodes(child, outNodes);
            }
        }
    };
}
