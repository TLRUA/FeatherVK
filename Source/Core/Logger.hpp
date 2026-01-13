#pragma once

#include <iostream>

namespace FeatherVK {
    class Logger {
    public:
        template<typename T>
        static void Info(const T &message) {
            Write("INFO", message);
        }

        template<typename T>
        static void Warn(const T &message) {
            Write("WARN", message);
        }

        template<typename T>
        static void Error(const T &message) {
            Write("ERROR", message);
        }

    private:
        template<typename T>
        static void Write(const char *level, const T &message) {
            std::cout << "[" << level << "] " << message << '\n';
        }
    };
}
