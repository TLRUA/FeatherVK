#pragma once

#include <filesystem>
#include <string>

namespace FeatherVK {
    class ProjectPaths {
    public:
        static std::string RootDir() {
            return EnsureTrailingSlash(FindProjectRoot());
        }

        static std::string ConfigurationsDir(const std::string &subdir = "") {
            std::filesystem::path path = FindProjectRoot() / "Configurations";
            if (!subdir.empty()) {
                path /= subdir;
            }
            return EnsureTrailingSlash(path);
        }

        static std::string TexturesDir(const std::string &subdir = "") {
            std::filesystem::path path = FindProjectRoot() / "Textures";
            if (!subdir.empty()) {
                path /= subdir;
            }
            return EnsureTrailingSlash(path);
        }

        static std::string ShadersDir(const std::string &subdir = "") {
            std::filesystem::path path = FindProjectRoot() / "Shaders";
            if (!subdir.empty()) {
                path /= subdir;
            }
            return EnsureTrailingSlash(path);
        }

        static std::string ModelsDir(const std::string &subdir = "") {
            std::filesystem::path path = FindProjectRoot() / "Models";
            if (!subdir.empty()) {
                path /= subdir;
            }
            return EnsureTrailingSlash(path);
        }

    private:
        static std::filesystem::path FindProjectRoot() {
            static const std::filesystem::path cachedRoot = []() {
                std::filesystem::path current = std::filesystem::current_path();
                for (int i = 0; i < 8; ++i) {
                    if (std::filesystem::exists(current / "Source") &&
                        std::filesystem::exists(current / "Configurations")) {
                        return current;
                    }
                    if (!current.has_parent_path()) {
                        break;
                    }
                    current = current.parent_path();
                }
                return std::filesystem::current_path();
            }();
            return cachedRoot;
        }

        static std::string EnsureTrailingSlash(const std::filesystem::path &path) {
            std::string text = path.lexically_normal().string();
            if (!text.empty() && text.back() != '/' && text.back() != '\\') {
                text.push_back('/');
            }
            return text;
        }
    };
}

