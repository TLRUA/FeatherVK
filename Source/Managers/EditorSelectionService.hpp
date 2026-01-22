#pragma once

#include <limits>

#include "../Utils/Utils.hpp"

namespace FeatherVK {
    class EditorSelectionService {
    public:
        inline static constexpr id_t InvalidEntityId = std::numeric_limits<id_t>::max();

        id_t GetSelectedId() const {
            return m_selectedEntityId;
        }

        bool HasSelection() const {
            return m_selectedEntityId != InvalidEntityId;
        }

        bool IsSelected(id_t entityId) const {
            return m_selectedEntityId == entityId;
        }

        void Select(id_t entityId) {
            m_selectedEntityId = entityId;
        }

        void ClearSelection() {
            m_selectedEntityId = InvalidEntityId;
        }

        void ClearIfSelected(id_t entityId) {
            if (m_selectedEntityId == entityId) {
                ClearSelection();
            }
        }

    private:
        id_t m_selectedEntityId = InvalidEntityId;
    };
}
