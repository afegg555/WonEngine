#pragma once

#include "Entity.h"
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::ecs
{
    class Scene;
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

    class WONENGINE_API ScriptRuntime
    {
    public:
        virtual ~ScriptRuntime() = default;

        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        virtual bool CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error) = 0;
        virtual void DestroyInstance(ScriptInstanceHandle handle) = 0;
        virtual bool ReloadScript(const String& script_path, String& out_error) = 0;

        virtual bool CallOnCreate(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error) = 0;
        virtual bool CallOnUpdate(ScriptInstanceHandle handle, const ScriptCallContext& context, float delta_time, String& out_error) = 0;
        virtual bool CallOnDestroy(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error) = 0;
    };

    WONENGINE_API std::shared_ptr<ScriptRuntime> CreateScriptRuntime(const ScriptRuntimeDesc& desc);
}
