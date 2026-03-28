#pragma once

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../Components/Component.hpp"
#include "../Utils/Utils.hpp"

namespace FeatherVK::ECS {
    using EntityId = id_t;

    struct EntityMeta {
        std::string name{"Entity"};
        bool active{true};
        bool onDisabled{false};
        bool onEnabled{false};
    };

    class SceneRegistry {
    public:
        EntityId CreateEntity(std::string name = "Entity", bool active = true) {
            const EntityId entityId = m_nextEntityId++;
            return RegisterEntity(entityId, std::move(name), active);
        }

        EntityId CreateEntityWithId(EntityId entityId, std::string name = "Entity", bool active = true) {
            if (m_entities.find(entityId) != m_entities.end()) {
                throw std::runtime_error("Entity id already exists in SceneRegistry");
            }

            if (entityId >= m_nextEntityId) {
                m_nextEntityId = entityId + 1;
            }

            return RegisterEntity(entityId, std::move(name), active);
        }

        bool DestroyEntity(EntityId entityId) {
            auto entityIt = m_entities.find(entityId);
            if (entityIt == m_entities.end()) {
                return false;
            }

            m_entities.erase(entityIt);
            m_componentsByEntity.erase(entityId);
            for (auto &poolEntry: m_componentPools) {
                poolEntry.second.Remove(entityId);
            }

            auto orderIt = std::find(m_entityOrder.begin(), m_entityOrder.end(), entityId);
            if (orderIt != m_entityOrder.end()) {
                m_entityOrder.erase(orderIt);
            }
            return true;
        }

        void Clear() {
            m_entities.clear();
            m_entityOrder.clear();
            m_componentsByEntity.clear();
            m_componentPools.clear();
            m_nextEntityId = 0;
        }

        bool IsAlive(EntityId entityId) const {
            return m_entities.find(entityId) != m_entities.end();
        }

        const std::unordered_map<EntityId, EntityMeta> &GetEntities() const {
            return m_entities;
        }

        const std::vector<EntityId> &GetEntityOrder() const {
            return m_entityOrder;
        }

        void SetEntityName(EntityId entityId, std::string name) {
            auto &meta = RequireEntity(entityId);
            meta.name = std::move(name);
        }

        const std::string &GetEntityName(EntityId entityId) const {
            return RequireEntity(entityId).name;
        }

        void SetEntityActive(EntityId entityId, bool active) {
            auto &meta = RequireEntity(entityId);
            meta.active = active;
        }

        bool IsEntityActive(EntityId entityId) const {
            return RequireEntity(entityId).active;
        }

        EntityMeta *TryGetEntityMeta(EntityId entityId) {
            auto entry = m_entities.find(entityId);
            return entry == m_entities.end() ? nullptr : &entry->second;
        }

        const EntityMeta *TryGetEntityMeta(EntityId entityId) const {
            auto entry = m_entities.find(entityId);
            return entry == m_entities.end() ? nullptr : &entry->second;
        }

        void AddComponent(EntityId entityId, Component *component) {
            if (component == nullptr) {
                throw std::runtime_error("Cannot add null component to SceneRegistry");
            }
            AddComponentByType(entityId, std::type_index(typeid(*component)), component);
        }

        template<typename T, typename std::enable_if<std::is_base_of<Component, T>::value, int>::type = 0>
        void AddComponent(EntityId entityId, T *component) {
            if (component == nullptr) {
                throw std::runtime_error("Cannot add null component to SceneRegistry");
            }
            AddComponentByType(entityId, std::type_index(typeid(T)), component);
        }

        template<typename T>
        bool TryGetComponent(EntityId entityId, T *&component) const {
            const ComponentPool *pool = GetPool(std::type_index(typeid(T)));
            if (pool == nullptr) {
                component = nullptr;
                return false;
            }

            Component *rawComponent = pool->TryGet(entityId);
            if (rawComponent == nullptr) {
                component = nullptr;
                return false;
            }

            component = dynamic_cast<T *>(rawComponent);
            return component != nullptr;
        }

        template<typename T>
        bool HasComponent(EntityId entityId) const {
            const ComponentPool *pool = GetPool(std::type_index(typeid(T)));
            return pool != nullptr && pool->TryGet(entityId) != nullptr;
        }

        template<typename T>
        bool RemoveComponent(EntityId entityId) {
            ComponentPool *pool = GetPool(std::type_index(typeid(T)));
            if (pool == nullptr || pool->TryGet(entityId) == nullptr) {
                return false;
            }

            pool->Remove(entityId);
            auto entityIt = m_componentsByEntity.find(entityId);
            if (entityIt != m_componentsByEntity.end()) {
                auto &components = entityIt->second;
                components.erase(std::remove_if(components.begin(), components.end(), [](Component *component) {
                    return dynamic_cast<T *>(component) != nullptr;
                }), components.end());
            }
            return true;
        }

        const std::vector<Component *> &GetComponents(EntityId entityId) const {
            static const std::vector<Component *> empty;
            auto entry = m_componentsByEntity.find(entityId);
            return entry == m_componentsByEntity.end() ? empty : entry->second;
        }

        template<typename... Ts>
        std::vector<EntityId> View() const {
            std::vector<EntityId> entities;
            entities.reserve(m_entityOrder.size());
            for (const auto entityId: m_entityOrder) {
                if ((HasComponent<Ts>(entityId) && ...)) {
                    entities.push_back(entityId);
                }
            }
            return entities;
        }

    private:
        EntityId RegisterEntity(EntityId entityId, std::string name, bool active) {
            m_entities.emplace(entityId, EntityMeta{std::move(name), active});
            m_entityOrder.push_back(entityId);
            return entityId;
        }

        struct ComponentPool {
            std::vector<EntityId> denseEntities{};
            std::vector<Component *> denseComponents{};
            std::unordered_map<EntityId, size_t> sparse{};

            void Add(EntityId entityId, Component *component) {
                auto sparseIt = sparse.find(entityId);
                if (sparseIt != sparse.end()) {
                    denseComponents[sparseIt->second] = component;
                    return;
                }

                const size_t denseIndex = denseEntities.size();
                denseEntities.push_back(entityId);
                denseComponents.push_back(component);
                sparse.emplace(entityId, denseIndex);
            }

            Component *TryGet(EntityId entityId) const {
                auto sparseIt = sparse.find(entityId);
                if (sparseIt == sparse.end()) {
                    return nullptr;
                }
                return denseComponents[sparseIt->second];
            }

            void Remove(EntityId entityId) {
                auto sparseIt = sparse.find(entityId);
                if (sparseIt == sparse.end()) {
                    return;
                }

                const size_t removedIndex = sparseIt->second;
                const size_t lastIndex = denseEntities.size() - 1;
                if (removedIndex != lastIndex) {
                    denseEntities[removedIndex] = denseEntities[lastIndex];
                    denseComponents[removedIndex] = denseComponents[lastIndex];
                    sparse[denseEntities[removedIndex]] = removedIndex;
                }

                denseEntities.pop_back();
                denseComponents.pop_back();
                sparse.erase(sparseIt);
            }
        };

        void AddComponentByType(EntityId entityId, const std::type_index &typeKey, Component *component) {
            RequireEntity(entityId);

            auto &components = m_componentsByEntity[entityId];
            for (auto *existingComponent: components) {
                if (existingComponent != nullptr && existingComponent->GetName() == component->GetName()) {
                    throw std::runtime_error("Entity already has component type " + component->GetName());
                }
            }

            components.push_back(component);
            m_componentPools[typeKey].Add(entityId, component);
        }

        EntityMeta &RequireEntity(EntityId entityId) {
            auto entry = m_entities.find(entityId);
            if (entry == m_entities.end()) {
                throw std::runtime_error("Entity not found in SceneRegistry");
            }
            return entry->second;
        }

        const EntityMeta &RequireEntity(EntityId entityId) const {
            auto entry = m_entities.find(entityId);
            if (entry == m_entities.end()) {
                throw std::runtime_error("Entity not found in SceneRegistry");
            }
            return entry->second;
        }

        ComponentPool *GetPool(const std::type_index &typeKey) {
            auto entry = m_componentPools.find(typeKey);
            return entry == m_componentPools.end() ? nullptr : &entry->second;
        }

        const ComponentPool *GetPool(const std::type_index &typeKey) const {
            auto entry = m_componentPools.find(typeKey);
            return entry == m_componentPools.end() ? nullptr : &entry->second;
        }

        EntityId m_nextEntityId = 0;
        std::unordered_map<EntityId, EntityMeta> m_entities{};
        std::vector<EntityId> m_entityOrder{};
        std::unordered_map<EntityId, std::vector<Component *>> m_componentsByEntity{};
        std::unordered_map<std::type_index, ComponentPool> m_componentPools{};
    };
}

