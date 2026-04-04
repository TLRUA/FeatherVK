#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "../Device.hpp"
#include "../Model.hpp"
#include "../RenderCore/RenderResourceRegistry.hpp"

namespace FeatherVK {
    class ModelRepository {
    public:
        explicit ModelRepository(Device &device) : m_device(device) {}

        void SetRenderResourceRegistry(RenderCore::RenderResourceRegistry *registry) {
            m_renderResourceRegistry = registry;
            for (auto &[name, model]: m_models) {
                RegisterMeshResource(name, model);
            }
        }

        Device &GetDevice() const {
            return m_device;
        }

        std::shared_ptr<Model> Find(const std::string &name) const {
            const auto entry = m_models.find(name);
            return entry == m_models.end() ? nullptr : entry->second;
        }

        std::shared_ptr<Model> GetOrLoad(const std::string &name, const std::string &relativePath) {
            if (auto existing = Find(name); existing != nullptr) {
                return existing;
            }

            auto model = Model::createModelFromFile(m_device, Model::GetBaseModelsPath() + relativePath);
            if (model != nullptr) {
                model->SetName(name);
                m_models.emplace(name, model);
                RegisterMeshResource(name, model, relativePath);
            }
            return model;
        }

        std::shared_ptr<Model> Store(const std::string &name, std::shared_ptr<Model> model) {
            if (model != nullptr) {
                model->SetName(name);
            }
            m_models[name] = std::move(model);
            RegisterMeshResource(name, m_models[name]);
            return m_models[name];
        }

        [[nodiscard]] RenderCore::RenderResourceHandle FindMeshResource(const std::string &name) const {
            if (m_renderResourceRegistry == nullptr) {
                return RenderCore::InvalidRenderResourceHandle;
            }
            return m_renderResourceRegistry->FindByName(RenderCore::RenderResourceType::Mesh, MakeMeshResourceName(name));
        }

        void Clear() {
            m_models.clear();
        }

    private:
        static std::string MakeMeshResourceName(const std::string &name) {
            return "Mesh/" + name;
        }

        void RegisterMeshResource(const std::string &name,
                                  const std::shared_ptr<Model> &model,
                                  const std::string &sourcePath = {}) {
            if (m_renderResourceRegistry == nullptr || model == nullptr) {
                return;
            }
            m_renderResourceRegistry->ImportMesh(MakeMeshResourceName(name), model, sourcePath, name);
        }

        Device &m_device;
        std::unordered_map<std::string, std::shared_ptr<Model>> m_models{};
        RenderCore::RenderResourceRegistry *m_renderResourceRegistry = nullptr;
    };
}
