#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

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
        SceneRegistry() = default;

        template<typename T, typename std::enable_if<std::is_base_of<Component, T>::value, int>::type = 0>
        void RegisterComponentType() {
            m_componentAdders[std::type_index(typeid(T))] = [](SceneRegistry &sceneRegistry, entt::entity entity, Component *component) {
                sceneRegistry.AddTypedComponent(entity, static_cast<T *>(component));
            };
        }

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

            std::unordered_set<Component *> componentsToRelease{};
            if (const ComponentList *componentList = m_registry.try_get<ComponentList>(entityIt->second);
                componentList != nullptr) {
                for (Component *component: componentList->components) {
                    if (component != nullptr) {
                        componentsToRelease.insert(component);
                    }
                }
            }

            m_registry.destroy(entityIt->second);
            m_entityHandles.erase(entityIt);
            m_entities.erase(entityId);

            const auto orderIt = std::find(m_entityOrder.begin(), m_entityOrder.end(), entityId);
            if (orderIt != m_entityOrder.end()) {
                m_entityOrder.erase(orderIt);
            }

            if (!componentsToRelease.empty()) {
                m_ownedComponents.erase(
                        std::remove_if(
                                m_ownedComponents.begin(),
                                m_ownedComponents.end(),
                                [&componentsToRelease](const std::unique_ptr<Component> &componentOwner) {
                                    return componentOwner != nullptr &&
                                           componentsToRelease.find(componentOwner.get()) != componentsToRelease.end();
                                }),
                        m_ownedComponents.end());
            }
            return true;
        }

        void Clear() {
            m_registry.clear();
            m_entityHandles.clear();
            m_entities.clear();
            m_entityOrder.clear();
            m_ownedComponents.clear();
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

        void AddComponent(EntityId entityId, Component *component) {
            if (component == nullptr) {
                throw std::runtime_error("Cannot add null component to SceneRegistry");
            }

            const entt::entity entity = RequireEntityHandle(entityId);
            ValidateUniqueComponent(entity, component);

            const auto addFn = m_componentAdders.find(std::type_index(typeid(*component)));
            if (addFn == m_componentAdders.end()) {
                throw std::runtime_error("Component type is not registered in SceneRegistry: " + component->GetName());
            }

            addFn->second(*this, entity, component);
        }

        template<typename T, typename std::enable_if<std::is_base_of<Component, T>::value, int>::type = 0>
        void AddComponent(EntityId entityId, T *component) {
            if (component == nullptr) {
                throw std::runtime_error("Cannot add null component to SceneRegistry");
            }

            entt::entity entity = RequireEntityHandle(entityId);
            ValidateUniqueComponent(entity, component);
            AddTypedComponent(entity, component);
        }

        template<typename T, typename... Args, typename std::enable_if<std::is_base_of<Component, T>::value, int>::type = 0>
        T *AddOwnedComponent(EntityId entityId, Args &&...args) {
            auto componentOwner = std::make_unique<T>(std::forward<Args>(args)...);
            T *componentPtr = componentOwner.get();
            AddComponent<T>(entityId, componentPtr);
            m_ownedComponents.emplace_back(std::move(componentOwner));
            return componentPtr;
        }

        Component *AddOwnedComponent(EntityId entityId, std::unique_ptr<Component> componentOwner) {
            if (componentOwner == nullptr) {
                throw std::runtime_error("Cannot add null owned component to SceneRegistry");
            }

            Component *componentPtr = componentOwner.get();
            AddComponent(entityId, componentPtr);
            m_ownedComponents.emplace_back(std::move(componentOwner));
            return componentPtr;
        }

        template<typename T>
        bool TryGetComponent(EntityId entityId, T *&component) const {
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null) {
                component = nullptr;
                return false;
            }

            using Wrapper = ComponentPtr<T>;
            const Wrapper *wrapper = m_registry.try_get<Wrapper>(entity);
            if (wrapper == nullptr || wrapper->value == nullptr) {
                component = nullptr;
                return false;
            }

            component = wrapper->value;
            return true;
        }

        template<typename T>
        bool HasComponent(EntityId entityId) const {
            const entt::entity entity = ResolveEntity(entityId);
            return entity != entt::null && m_registry.template has<ComponentPtr<T>>(entity);
        }

        template<typename T>
        bool RemoveComponent(EntityId entityId) {
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null || !m_registry.template has<ComponentPtr<T>>(entity)) {
                return false;
            }

            using Wrapper = ComponentPtr<T>;
            T *toRemove = nullptr;
            if (const Wrapper *wrapper = m_registry.try_get<Wrapper>(entity); wrapper != nullptr) {
                toRemove = wrapper->value;
            }

            m_registry.remove<Wrapper>(entity);

            ComponentList *componentList = m_registry.try_get<ComponentList>(entity);
            if (componentList != nullptr) {
                auto &components = componentList->components;
                components.erase(
                        std::remove_if(
                                components.begin(),
                                components.end(),
                                [toRemove](Component *component) {
                                    return component == toRemove || dynamic_cast<T *>(component) != nullptr;
                                }),
                        components.end());
            }

            return true;
        }

        const std::vector<Component *> &GetComponents(EntityId entityId) const {
            static const std::vector<Component *> empty;
            const entt::entity entity = ResolveEntity(entityId);
            if (entity == entt::null) {
                return empty;
            }

            const ComponentList *componentList = m_registry.try_get<ComponentList>(entity);
            return componentList == nullptr ? empty : componentList->components;
        }

        template<typename... Ts>
        std::vector<EntityId> View() const {
            std::vector<EntityId> entities;
            auto view = m_registry.view<const EntityIdComponent, const ComponentPtr<Ts>...>();
            entities.reserve(m_entityOrder.size());
            for (const auto entity: view) {
                entities.push_back(view.template get<const EntityIdComponent>(entity).id);
            }
            return entities;
        }

    private:
        struct EntityIdComponent {
            EntityId id = 0;
        };

        struct ComponentList {
            std::vector<Component *> components{};
        };

        template<typename T>
        struct ComponentPtr {
            T *value = nullptr;
        };

        using ComponentAdder = std::function<void(SceneRegistry &, entt::entity, Component *)>;

        EntityId RegisterEntity(EntityId entityId, std::string name, bool active) {
            const entt::entity entity = m_registry.create();
            m_registry.emplace<EntityIdComponent>(entity, EntityIdComponent{entityId});
            m_registry.emplace<ComponentList>(entity);

            m_entityHandles.emplace(entityId, entity);
            m_entities.emplace(entityId, EntityMeta{std::move(name), active});
            m_entityOrder.push_back(entityId);
            return entityId;
        }

        template<typename T>
        void AddTypedComponent(entt::entity entity, T *component) {
            using Wrapper = ComponentPtr<T>;
            if (m_registry.template has<Wrapper>(entity)) {
                throw std::runtime_error("Entity already has component type " + component->GetName());
            }

            m_registry.emplace<Wrapper>(entity, Wrapper{component});
            m_registry.get<ComponentList>(entity).components.push_back(component);
        }

        void ValidateUniqueComponent(entt::entity entity, Component *component) const {
            const auto &components = m_registry.get<ComponentList>(entity).components;
            for (Component *existingComponent: components) {
                if (existingComponent != nullptr && existingComponent->GetName() == component->GetName()) {
                    throw std::runtime_error("Entity already has component type " + component->GetName());
                }
            }
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
        std::unordered_map<std::type_index, ComponentAdder> m_componentAdders{};
        std::vector<std::unique_ptr<Component>> m_ownedComponents{};
    };
}
