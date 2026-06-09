#pragma once

#include "ScriptRuntime.h"

struct lua_State;

namespace won::script
{
    enum class LuaScriptFunction : uint32
    {
        OnCreate,
        OnUpdate,
        OnDestroy,

        Count
    };

    struct LuaScriptModule
    {
        String script_path;
        int module_ref = 0;
        int function_refs[static_cast<uint32>(LuaScriptFunction::Count)] = {};
        uint32 ref_count = 0;
    };

    struct LuaScriptInstance
    {
        String script_path;
        int self_ref = 0;
    };

    class LuaScriptRuntime : public ScriptRuntime
    {
    public:
        bool Initialize() override;
        void Shutdown() override;

        bool CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error) override;
        void DestroyInstance(ScriptInstanceHandle handle) override;
        bool ReloadScript(const String& script_path, String& out_error) override;

        bool CallOnCreate(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error) override;
        bool CallOnUpdate(ScriptInstanceHandle handle, const ScriptCallContext& context, float delta_time, String& out_error) override;
        bool CallOnDestroy(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error) override;

    private:
        static int LuaLogInfo(lua_State* state);
        static int LuaLogWarn(lua_State* state);
        static int LuaLogError(lua_State* state);
        static int LuaEntityIsValid(lua_State* state);
        static int LuaEntityDestroy(lua_State* state);
        static int LuaEntityGetName(lua_State* state);
        static int LuaEntitySetName(lua_State* state);
        static int LuaTransformHas(lua_State* state);
        static int LuaTransformGetPosition(lua_State* state);
        static int LuaTransformSetPosition(lua_State* state);
        static int LuaTransformTranslate(lua_State* state);
        static int LuaTransformGetScale(lua_State* state);
        static int LuaTransformSetScale(lua_State* state);
        static int LuaTransformRotateEuler(lua_State* state);
        static int LuaInputIsKeyDown(lua_State* state);
        static int LuaInputIsKeyPressed(lua_State* state);
        static int LuaInputIsActionDown(lua_State* state);
        static int LuaInputIsActionPressed(lua_State* state);
        static int LuaInputGetActionValue(lua_State* state);
        static int LuaInputGetActionAxis2D(lua_State* state);
        static int LuaSceneFindByName(lua_State* state);

        void RegisterAPI();
        bool LoadModule(const String& script_path, LuaScriptModule& out_module, String& out_error);
        bool LoadFunctionRef(int module_ref, const char* function_name, int& out_function_ref, String& out_error);
        void UnloadModule(LuaScriptModule& module);
        bool CallFunction(ScriptInstanceHandle handle, LuaScriptFunction function, const ScriptCallContext& context, float delta_time, bool has_delta_time, String& out_error);

        lua_State* lua_state = nullptr;
        uint64 next_instance_id = 1;
        ScriptCallContext current_context = {};
        UnorderedMap<String, LuaScriptModule> modules;
        UnorderedMap<uint64, LuaScriptInstance> instances;
    };
}
