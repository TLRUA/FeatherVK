#pragma once

#include <deque>

namespace FeatherVK {
    enum class EventType {
        WindowResized
    };

    struct Event {
        EventType type{EventType::WindowResized};
        int width{0};
        int height{0};
    };

    class EventQueue {
    public:
        static void PushWindowResized(int width, int height) {
            s_events.push_back({EventType::WindowResized, width, height});
        }

        static bool Poll(Event &event) {
            if (s_events.empty()) {
                return false;
            }
            event = s_events.front();
            s_events.pop_front();
            return true;
        }

    private:
        inline static std::deque<Event> s_events{};
    };
}
