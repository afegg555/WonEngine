#pragma once

#include "EventHandler.h"
#include "ScriptRuntime.h"

struct lua_State;

namespace won::game
{
    class GameData;
}

namespace won::script
{
    inline constexpr uint32 lua_script_builtin_function_count = static_cast<uint32>(ScriptCallType::OnTriggerExit3D) + 1u;

    struct LuaScriptModule
    {
        String script_path;
        int module_ref = 0;
        int function_refs[lua_script_builtin_function_count] = {};
        UnorderedMap<String, int> custom_function_refs;
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
        explicit LuaScriptRuntime(const ScriptRuntimeDesc& desc);

        bool Initialize() override;
        void Shutdown() override;

        bool CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error) override;
        void DestroyInstance(ScriptInstanceHandle handle) override;
        bool ReloadScript(const String& script_path, String& out_error) override;

        bool Call(ScriptInstanceHandle handle, const ScriptCallDesc& desc, String& out_error) override;

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
        static int LuaMaterialHas(lua_State* state);
        static int LuaMaterialGetBaseColor(lua_State* state);
        static int LuaMaterialSetBaseColor(lua_State* state);
        static int LuaInputIsKeyDown(lua_State* state);
        static int LuaInputIsKeyPressed(lua_State* state);
        static int LuaInputIsActionDown(lua_State* state);
        static int LuaInputIsActionPressed(lua_State* state);
        static int LuaInputGetActionValue(lua_State* state);
        static int LuaInputGetActionAxis2D(lua_State* state);
        static int LuaSceneFindByName(lua_State* state);
        static int LuaEventSubscribe(lua_State* state);
        static int LuaEventPost(lua_State* state);
        static int LuaEventFire(lua_State* state);
        static int LuaGameDataGetString(lua_State* state);
        static int LuaGameDataSetString(lua_State* state);
        static int LuaGameDataGetInt(lua_State* state);
        static int LuaGameDataSetInt(lua_State* state);
        static int LuaGameDataGetFloat(lua_State* state);
        static int LuaGameDataSetFloat(lua_State* state);
        static int LuaGameDataGetBool(lua_State* state);
        static int LuaGameDataSetBool(lua_State* state);

        void RegisterAPI();
        bool LoadModule(const String& script_path, LuaScriptModule& out_module, String& out_error);
        bool LoadFunctionRef(int module_ref, const char* function_name, int& out_function_ref, String& out_error);
        void UnloadModule(LuaScriptModule& module);

        lua_State* lua_state = nullptr;
        uint64 next_instance_id = 1;
        ScriptCallContext current_context = {};
        Vector<String> output_strings;
        UnorderedMap<String, LuaScriptModule> modules;
        UnorderedMap<uint64, LuaScriptInstance> instances;
        Vector<eventhandler::Handle> event_handles;
        game::GameData* game_data = nullptr;
    };
}
