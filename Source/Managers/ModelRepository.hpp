#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "../Device.hpp"
#include "../Model.hpp"

namespace FeatherVK {
    class ModelRepository {
    public:
        explicit ModelRepository(Device &device) : m_device(device) {}

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
            }
            return model;
        }

        std::shared_ptr<Model> Store(const std::string &name, std::shared_ptr<Model> model) {
            if (model != nullptr) {
                model->SetName(name);
            }
            m_models[name] = std::move(model);
            return m_models[name];
        }

        void Clear() {
            m_models.clear();
        }

    private:
        Device &m_device;
        std::unordered_map<std::string, std::shared_ptr<Model>> m_models{};
    };
}
