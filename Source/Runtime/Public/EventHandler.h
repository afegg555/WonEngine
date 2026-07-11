#pragma once
#include "FunctionTypes.h"
#include "RuntimeExport.h"
#include "StableHash.h"
#include "Types.h"

#include <functional>
#include <memory>

namespace won::eventhandler
{
    constexpr uint64 HashEvent(const char* name) { return won::StableHash(name); }

    inline constexpr uint64 EVENT_THREAD_SAFE_POINT = HashEvent("won.thread_safe_point");
    inline constexpr uint64 EVENT_SCENE_LOAD        = HashEvent("won.scene.load");
    inline constexpr uint64 EVENT_PREFAB_SPAWN      = HashEvent("won.prefab.spawn");
    inline constexpr uint64 EVENT_PREFAB_PRELOAD    = HashEvent("won.prefab.preload");

    struct Handle
    {
        std::shared_ptr<void> internal_state;
        bool IsValid() const { return internal_state != nullptr; }
    };

    WONENGINE_API Handle Subscribe(uint64 id, std::function<void(const function::Value&)> callback);
    WONENGINE_API void SubscribeOnce(uint64 id, std::function<void(const function::Value&)> callback);

    WONENGINE_API void FireEvent(uint64 id, const function::Value& payload = {});
    WONENGINE_API void PostEvent(uint64 id, const function::Value& payload = {});
    WONENGINE_API void Dispatch();
}
