#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <functional>
#include <memory>

namespace won::eventhandler
{
    inline constexpr int EVENT_THREAD_SAFE_POINT = -1;
    inline constexpr int EVENT_RELOAD_SHADERS = -2;
    inline constexpr int EVENT_SET_VSYNC = -3;

    struct Handle
    {
        std::shared_ptr<void> internal_state;
        bool IsValid() const { return internal_state.get() != nullptr; }
    };

    WONENGINE_API Handle Subscribe(int id, std::function<void(uint64)> callback);
    WONENGINE_API void SubscribeOnce(int id, std::function<void(uint64)> callback);
    WONENGINE_API void FireEvent(int id, uint64 userdata);

    inline void Subscribe_Once(int id, std::function<void(uint64)> callback)
    {
        SubscribeOnce(id, std::move(callback));
    }

    inline void SetVSync(bool enabled)
    {
        FireEvent(EVENT_SET_VSYNC, enabled ? 1ull : 0ull);
    }
}
