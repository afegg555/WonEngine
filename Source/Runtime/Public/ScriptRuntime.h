#pragma once

#include "Entity.h"
#include "FunctionTypes.h"
#include "RuntimeExport.h"
#include "Types.h"
#include "MathTypes.h"

#include <functional>
#include <memory>

namespace won::ecs
{
    class Scene;
}

namespace won::game
{
    class GameData;
}

namespace won::audio
{
    class AudioMixer;
}

namespace won::rendering
{
    class View;
}

namespace won::script
{
    enum class ScriptBackend
    {
        Lua,
    };

    struct ScriptRuntimeDesc
    {
        ScriptBackend backend = ScriptBackend::Lua;
        game::GameData* game_data = nullptr;
        audio::AudioMixer* audio_mixer = nullptr;
        String content_root;
    };

    struct ScriptInstanceHandle
    {
        uint64 value = 0;

        bool IsValid() const
        {
            return value != 0;
        }
    };

    struct ScriptInstanceDesc
    {
        String script_path;
    };

    struct ScriptCallContext
    {
        ecs::Scene* scene = nullptr;
        ecs::Entity entity = ecs::INVALID_ENTITY;
    };

    enum class ScriptCallType
    {
		// engine-defined events
        OnCreate,
        OnUpdate,
        OnDestroy,
        OnTriggerEnter3D,
        OnTriggerStay3D,
        OnTriggerExit3D,
        OnClick,
        OnAnimationEvent,

		// user-defined
        Custom,
    };

    struct ScriptCallDesc
    {
        ScriptCallType type = ScriptCallType::Custom;
        ScriptCallContext context = {};
        const char* function_name = nullptr;
        const won::function::Call* call = nullptr;
    };

    class WONENGINE_API ScriptRuntime
    {
    public:
        virtual ~ScriptRuntime() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual bool CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error) = 0;
        virtual void DestroyInstance(ScriptInstanceHandle handle) = 0;
        virtual bool ReloadScript(const String& script_path, String& out_error) = 0;

        virtual bool Call(ScriptInstanceHandle handle, const ScriptCallDesc& desc, String& out_error) = 0;

        virtual void SetViewResolver(std::function<rendering::View*(float2)> resolver) {}
    };

    WONENGINE_API std::shared_ptr<ScriptRuntime> CreateScriptRuntime(const ScriptRuntimeDesc& desc);
}
