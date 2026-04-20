#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "../Utils/Utils.hpp"

namespace FeatherVK::ECS {
    using EntityId = id_t;

    struct EntityMeta {
        std::string name{"Entity"};
        bool active{true};
    };

    class SceneRegistry {
    public:
        SceneRegistry() = default;

        EntityId CreateEntity(std::string name = "Entity", bool active = true) {
            const EntityId entityId = m_nextEntityId++;
            return RegisterEntity(entityId, std::move(name), active);
        }

        EntityId CreateEntityWithId(EntityId entityId, std::string name = "Entity", bool active = true) {
            if (m_entityHandles.find(entityId) != m_entityHandles.end()) {
                throw std::runtime_error("Entity id already exists in SceneRegistry");
            }

            if (entityId >= m_nextEntityId) {
                m_nextEntityId = entityId + 1;
            }

            return RegisterEntity(entityId, std::move(name), active);
        }

        bool DestroyEntity(EntityId entityId) {
            const auto entityIt = m_entityHandles.find(entityId);
            if (entityIt == m_entityHandles.end()) {
                return false;
            }

            m_registry.destroy(entityIt->second);
            m_entityHandles.erase(entityIt);
            m_entities.erase(entityId);

            const auto orderIt = std::find(m_entityOrder.begin(), m_entityOrder.end(), entityId);
            if (orderIt != m_entityOrder.end()) {
                m_entityOrder.erase(orderIt);
            }

            return true;
        }

        void Clear() {
            m_registry.clear();
            m_entityHandles.clear();
            m_entities.clear();
            m_entityOrder.clear();
            m_nextEntityId = 0;
        }

        bool IsAlive(EntityId entityId) const {
            const auto entityIt = m_entityHandles.find(entityId);
            return entityIt != m_entityHandles.end() && m_registry.valid(entityIt->second);
        }

        const std::unordered_map<EntityId, EntityMeta> &GetEntities() const {
            return m_entities;
        }

        const std::vector<EntityId> &GetEntityOrder() const {
            return m_entityOrder;
        }

        void SetEntityName(EntityId entityId, std::string name) {
            auto &meta = RequireEntityMeta(entityId);
            meta.name = std::move(name);
        }

        const std::string &GetEntityName(EntityId entityId) const {
            return RequireEntityMeta(entityId).name;
        }

        void SetEntityActive(EntityId entityId, bool active) {
            auto &meta = RequireEntityMeta(entityId);
            meta.active = active;
        }

        bool IsEntityActive(EntityId entityId) const {
            return RequireEntityMeta(entityId).active;
        }

        EntityMeta *TryGetEntityMeta(EntityId entityId) {
            const auto entry = m_entities.find(entityId);
            return entry == m_entities.end() ? nullptr : &entry->second;
        }

        const EntityMeta *TryGetEntityMeta(EntityId entityId) const {
            const auto entry = m_entities.find(entityId);
            return entry == m_entities.end() ? nullptr : &entry->second;
        }

        template<typename T, typename... Args>
        T *EmplaceComponent(EntityId entityId, Args &&...args) {
            const entt::entity entity = RequireEntityHandle(entityId);
            if (m_registry.template has<T>(entity)) {
                throw std::runtime_error("Entity already has component type");
            }

            T &component = m_registry.template emplace<T>(entity, std::forward<Args>(args)...);
            return &component;
        }

        template<typename T>
        bool TryGetComponent(EntityId entityId, T *&component) const {
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null) {
                component = nullptr;
                return false;
            }

            const T *storedComponent = m_registry.template try_get<T>(entity);
            if (storedComponent == nullptr) {
                component = nullptr;
                return false;
            }

            component = const_cast<T *>(storedComponent);
            return true;
        }

        template<typename T>
        T *TryGetComponent(EntityId entityId) const {
            T *component = nullptr;
            return TryGetComponent(entityId, component) ? component : nullptr;
        }

        template<typename T>
        bool HasComponent(EntityId entityId) const {
            const entt::entity entity = ResolveEntity(entityId);
            return entity != entt::null && m_registry.template has<T>(entity);
        }

        template<typename T>
        bool RemoveComponent(EntityId entityId) {
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null || !m_registry.template has<T>(entity)) {
                return false;
            }

            m_registry.template remove<T>(entity);
            return true;
        }

        template<typename... Ts>
        std::vector<EntityId> View() const {
            std::vector<EntityId> entities;
            auto view = m_registry.view<const EntityIdComponent, std::add_const_t<std::remove_reference_t<Ts>>...>();
            entities.reserve(m_entityOrder.size());
            for (const auto entity: view) {
                entities.push_back(view.template get<const EntityIdComponent>(entity).id);
            }
            return entities;
        }

        template<typename... Ts>
        size_t ViewSizeHint() const {
            return m_entityOrder.size();
        }

        template<typename... Ts, typename Func>
        void ForEachView(Func &&func) const {
            auto view = m_registry.view<const EntityIdComponent, std::add_const_t<std::remove_reference_t<Ts>>...>();
            for (const auto entity: view) {
                func(view.template get<const EntityIdComponent>(entity).id);
            }
        }

    private:
        struct EntityIdComponent {
            EntityId id = 0;
        };

        EntityId RegisterEntity(EntityId entityId, std::string name, bool active) {
            const entt::entity entity = m_registry.create();
            m_registry.emplace<EntityIdComponent>(entity, EntityIdComponent{entityId});

            m_entityHandles.emplace(entityId, entity);
            m_entities.emplace(entityId, EntityMeta{std::move(name), active});
            m_entityOrder.push_back(entityId);
            return entityId;
        }

        entt::entity ResolveEntity(EntityId entityId) const {
            const auto entityIt = m_entityHandles.find(entityId);
            if (entityIt == m_entityHandles.end()) {
                return entt::null;
            }
            return m_registry.valid(entityIt->second) ? entityIt->second : entt::null;
        }

        entt::entity RequireEntityHandle(EntityId entityId) const {
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null) {
                throw std::runtime_error("Entity not found in SceneRegistry");
            }
            return entity;
        }

        EntityMeta &RequireEntityMeta(EntityId entityId) {
            auto entry = m_entities.find(entityId);
            if (entry == m_entities.end()) {
                throw std::runtime_error("Entity not found in SceneRegistry");
            }
            return entry->second;
        }

        const EntityMeta &RequireEntityMeta(EntityId entityId) const {
            auto entry = m_entities.find(entityId);
            if (entry == m_entities.end()) {
                throw std::runtime_error("Entity not found in SceneRegistry");
            }
            return entry->second;
        }

        EntityId m_nextEntityId = 0;
        entt::registry m_registry{};
        std::unordered_map<EntityId, entt::entity> m_entityHandles{};
        std::unordered_map<EntityId, EntityMeta> m_entities{};
        std::vector<EntityId> m_entityOrder{};
    };
}
