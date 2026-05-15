#include "LuaScriptRuntime.h"

#include "Backlog.h"
#include "Input.h"
#include "NameComponent.h"
#include "Scene.h"
#include "TransformComponent.h"

extern "C"
{
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace won::script
{
    namespace
    {
        constexpr int lua_no_ref = LUA_NOREF;

        struct LuaStackGuard
        {
            lua_State* state = nullptr;
            int top = 0;

            explicit LuaStackGuard(lua_State* state_in)
                : state(state_in), top(lua_gettop(state_in)) // get count of values in stack
            {
            }

            ~LuaStackGuard()
            {
                lua_settop(state, top); // set count of values in stack
            }
        };

        const char* GetFunctionName(LuaScriptFunction function)
        {
            switch (function)
            {
            case LuaScriptFunction::OnCreate:
                return "OnCreate";
            case LuaScriptFunction::OnUpdate:
                return "OnUpdate";
            case LuaScriptFunction::OnDestroy:
                return "OnDestroy";
            default:
                return "";
            }
        }

        uint32 GetFunctionIndex(LuaScriptFunction function)
        {
            return static_cast<uint32>(function);
        }
    }

    bool LuaScriptRuntime::Initialize()
    {
        lua_state = luaL_newstate();
        if (!lua_state)
        {
            return false;
        }

        luaL_openlibs(lua_state);
        RegisterAPI();
        return true;
    }

    void LuaScriptRuntime::Shutdown()
    {
        if (!lua_state)
        {
            return;
        }

        for (auto& entry : instances)
        {
            if (entry.second.self_ref != lua_no_ref)
            {
                luaL_unref(lua_state, LUA_REGISTRYINDEX, entry.second.self_ref); // releases the registry reference so Lua can collect it
                entry.second.self_ref = lua_no_ref;
            }
        }
        instances.clear();

        for (auto& entry : modules)
        {
            UnloadModule(entry.second);
        }
        modules.clear();

        if (lua_state)
        {
            lua_close(lua_state);
            lua_state = nullptr;
        }
    }

    bool LuaScriptRuntime::CreateInstance(const ScriptInstanceDesc& desc, ScriptInstanceHandle& out_handle, String& out_error)
    {
        if (!lua_state)
        {
            out_handle = {};
            out_error = "Lua state is not initialized.";
            return false;
        }

        if (desc.script_path.empty())
        {
            out_handle = {};
            out_error = "Script path is empty.";
            return false;
        }

        auto module_it = modules.find(desc.script_path);
        if (module_it == modules.end())
        {
            LuaScriptModule module = {};
            if (!LoadModule(desc.script_path, module, out_error))
            {
                out_handle = {};
                return false;
            }

            module.ref_count = 0;
            module_it = modules.emplace(desc.script_path, module).first;
        }

        lua_newtable(lua_state); // creates the per-instance self table on top of the stack
        const int self_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX); // stores the self table in the registry
        if (self_ref == lua_no_ref)
        {
            out_handle = {};
            out_error = "Failed to create script self table.";
            return false;
        }

        ++module_it->second.ref_count;
        const uint64 instance_id = next_instance_id++;
        LuaScriptInstance instance = {};
        instance.script_path = desc.script_path;
        instance.self_ref = self_ref;
        instances[instance_id] = instance;
        out_handle.value = instance_id;
        out_error.clear();
        return true;
    }

    void LuaScriptRuntime::DestroyInstance(ScriptInstanceHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        auto it = instances.find(handle.value);
        if (it == instances.end())
        {
            return;
        }

        if (it->second.self_ref != lua_no_ref)
        {
            luaL_unref(lua_state, LUA_REGISTRYINDEX, it->second.self_ref); // releases the registry reference so Lua can collect it
            it->second.self_ref = lua_no_ref;
        }

        auto module_it = modules.find(it->second.script_path);
        if (module_it != modules.end())
        {
            if (module_it->second.ref_count > 0)
            {
                --module_it->second.ref_count;
            }

            if (module_it->second.ref_count == 0)
            {
                UnloadModule(module_it->second);
                modules.erase(module_it);
            }
        }

        instances.erase(it);
    }

    bool LuaScriptRuntime::ReloadScript(const String& script_path, String& out_error)
    {
        if (script_path.empty())
        {
            out_error = "Script path is empty.";
            return false;
        }

        auto it = modules.find(script_path);
        if (it == modules.end())
        {
            out_error.clear();
            return true;
        }

        LuaScriptModule new_module = {};
        if (!LoadModule(script_path, new_module, out_error))
        {
            return false;
        }

        new_module.ref_count = it->second.ref_count;
        UnloadModule(it->second);
        it->second = new_module;
        out_error.clear();
        return true;
    }

    bool LuaScriptRuntime::CallOnCreate(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error)
    {
        return CallFunction(handle, LuaScriptFunction::OnCreate, context, 0.0f, false, out_error);
    }

    bool LuaScriptRuntime::CallOnUpdate(ScriptInstanceHandle handle, const ScriptCallContext& context, float delta_time, String& out_error)
    {
        return CallFunction(handle, LuaScriptFunction::OnUpdate, context, delta_time, true, out_error);
    }

    bool LuaScriptRuntime::CallOnDestroy(ScriptInstanceHandle handle, const ScriptCallContext& context, String& out_error)
    {
        return CallFunction(handle, LuaScriptFunction::OnDestroy, context, 0.0f, false, out_error);
    }

    int LuaScriptRuntime::LuaLogInfo(lua_State* state)
    {
        const char* message = luaL_checkstring(state, 1);
        won::backlog::Post(message);
        return 0;
    }

    int LuaScriptRuntime::LuaLogWarn(lua_State* state)
    {
        const char* message = luaL_checkstring(state, 1);
        won::backlog::Post(message, won::backlog::LogLevel::Warning);
        return 0;
    }

    int LuaScriptRuntime::LuaLogError(lua_State* state)
    {
        const char* message = luaL_checkstring(state, 1);
        won::backlog::Post(message, won::backlog::LogLevel::Error);
        return 0;
    }

    int LuaScriptRuntime::LuaEntityIsValid(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const Vector<ecs::Entity>& entities = runtime->current_context.scene->GetEntities();
        lua_pushboolean(state, std::find(entities.begin(), entities.end(), entity) != entities.end());
        return 1;
    }

    int LuaScriptRuntime::LuaEntityDestroy(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const Vector<ecs::Entity>& entities = runtime->current_context.scene->GetEntities();
        if (std::find(entities.begin(), entities.end(), entity) == entities.end())
        {
            lua_pushboolean(state, false);
            return 1;
        }

        runtime->current_context.scene->DestroyEntity(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEntityGetName(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::NameComponent* name = runtime->current_context.scene->GetComponent<ecs::NameComponent>(entity);
        if (!name)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushstring(state, name->value.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaEntitySetName(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 2 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const char* value = luaL_checkstring(state, has_entity_arg ? 2 : 1);
        ecs::NameComponent* name = runtime->current_context.scene->GetComponent<ecs::NameComponent>(entity);
        if (!name)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        name->value = value;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformGetPosition(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, transform->position.x);
        lua_pushnumber(state, transform->position.y);
        lua_pushnumber(state, transform->position.z);
        return 3;
    }

    int LuaScriptRuntime::LuaTransformSetPosition(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 4 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        transform->position.x = static_cast<float>(luaL_checknumber(state, value_index));
        transform->position.y = static_cast<float>(luaL_checknumber(state, value_index + 1));
        transform->position.z = static_cast<float>(luaL_checknumber(state, value_index + 2));
        transform->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformTranslate(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 4 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        transform->Translate(float3(
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformGetScale(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, transform->scale.x);
        lua_pushnumber(state, transform->scale.y);
        lua_pushnumber(state, transform->scale.z);
        return 3;
    }

    int LuaScriptRuntime::LuaTransformSetScale(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 4 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        transform->scale.x = static_cast<float>(luaL_checknumber(state, value_index));
        transform->scale.y = static_cast<float>(luaL_checknumber(state, value_index + 1));
        transform->scale.z = static_cast<float>(luaL_checknumber(state, value_index + 2));
        transform->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformRotateEuler(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 4 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        transform->RotateRollPitchYaw(float3(
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaInputIsKeyDown(lua_State* state)
    {
        const io::Button button = io::GetButtonFromString(luaL_checkstring(state, 1));
        lua_pushboolean(state, button != io::BUTTON_NONE && io::IsDown(button));
        return 1;
    }

    int LuaScriptRuntime::LuaInputIsKeyPressed(lua_State* state)
    {
        const io::Button button = io::GetButtonFromString(luaL_checkstring(state, 1));
        lua_pushboolean(state, button != io::BUTTON_NONE && io::IsPressed(button));
        return 1;
    }

    int LuaScriptRuntime::LuaSceneFindByName(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const char* value = luaL_checkstring(state, 1);
        auto name_array = runtime->current_context.scene->GetComponentArray<ecs::NameComponent>();
        if (!name_array)
        {
            lua_pushnil(state);
            return 1;
        }

        for (Size i = 0; i < name_array->GetSize(); ++i)
        {
            if (name_array->data[i].value == value)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(name_array->index_to_entity[i]));
                return 1;
            }
        }

        lua_pushnil(state);
        return 1;
    }

    void LuaScriptRuntime::RegisterAPI()
    {
        if (!lua_state)
        {
            return;
        }

        LuaStackGuard stack_guard(lua_state);
        lua_newtable(lua_state); // stack: [won_table]
        lua_newtable(lua_state); // stack: [won_table, log_table]
        lua_pushcfunction(lua_state, LuaLogInfo); // stack: [won_table, log_table, LuaLogInfo]
        lua_setfield(lua_state, -2, "info"); // stack: [won_table, log_table(.info = LuaLogInfo)]
        lua_pushcfunction(lua_state, LuaLogWarn);
        lua_setfield(lua_state, -2, "warn");
        lua_pushcfunction(lua_state, LuaLogError);
        lua_setfield(lua_state, -2, "error");
        lua_setfield(lua_state, -2, "log"); // stack: [won_table(.log = log_table)]

        lua_newtable(lua_state); // stack: [won_table, entity_table]
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntityIsValid, 1);
        lua_setfield(lua_state, -2, "is_valid");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntityDestroy, 1);
        lua_setfield(lua_state, -2, "destroy");
        lua_pushlightuserdata(lua_state, this); // stack: [won_table, entity_table, runtime_pointer]
        lua_pushcclosure(lua_state, LuaEntityGetName, 1); // [won_table, entity_table, closure(with 1 upvalue capture)]
        lua_setfield(lua_state, -2, "get_name"); // ...
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntitySetName, 1);
        lua_setfield(lua_state, -2, "set_name");
        lua_setfield(lua_state, -2, "entity");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformGetPosition, 1);
        lua_setfield(lua_state, -2, "get_position");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformSetPosition, 1);
        lua_setfield(lua_state, -2, "set_position");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformTranslate, 1);
        lua_setfield(lua_state, -2, "translate");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformGetScale, 1);
        lua_setfield(lua_state, -2, "get_scale");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformSetScale, 1);
        lua_setfield(lua_state, -2, "set_scale");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformRotateEuler, 1);
        lua_setfield(lua_state, -2, "rotate_euler");
        lua_setfield(lua_state, -2, "transform");

        lua_newtable(lua_state);
        lua_pushcfunction(lua_state, LuaInputIsKeyDown);
        lua_setfield(lua_state, -2, "is_key_down");
        lua_pushcfunction(lua_state, LuaInputIsKeyPressed);
        lua_setfield(lua_state, -2, "is_key_pressed");
        lua_setfield(lua_state, -2, "input");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneFindByName, 1);
        lua_setfield(lua_state, -2, "find_by_name");
        lua_setfield(lua_state, -2, "scene");

        lua_setglobal(lua_state, "won");
    }

    bool LuaScriptRuntime::LoadModule(const String& script_path, LuaScriptModule& out_module, String& out_error)
    {
        out_module = {};
        out_module.script_path = script_path;
        out_module.module_ref = lua_no_ref;
        for (uint32 i = 0; i < static_cast<uint32>(LuaScriptFunction::Count); ++i)
        {
            out_module.function_refs[i] = lua_no_ref;
        }

        if (!lua_state)
        {
            out_error = "Lua state is not initialized.";
            return false;
        }

        if (script_path.empty())
        {
            out_error = "Script path is empty.";
            return false;
        }

        LuaStackGuard stack_guard(lua_state);
        if (luaL_loadfile(lua_state, script_path.c_str()) != LUA_OK) // loads and compiles the file as a chunk function on top of the stack
        {
            const char* error = lua_tostring(lua_state, -1);
            out_error = error ? error : "Failed to load Lua script.";
            return false;
        }

        if (lua_pcall(lua_state, 0, 1, 0) != LUA_OK) // calls the chunk(spend) with no args and keeps one returned value(table) on the stack
        {
            const char* error = lua_tostring(lua_state, -1);
            out_error = error ? error : "Failed to run Lua script.";
            return false;
        }

        if (!lua_istable(lua_state, -1)) // the script file must return its module table
        {
            out_error = "Lua script must return a table.";
            return false;
        }

        // lua_pushvalue(lua_state, -1); // duplicates the module table so one copy can be stored as module_ref
        out_module.module_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX); // stores the stack top in the registry and returns an integer reference

        for (uint32 i = 0; i < static_cast<uint32>(LuaScriptFunction::Count); ++i)
        {
            LuaScriptFunction function = static_cast<LuaScriptFunction>(i);
            if (!LoadFunctionRef(out_module.module_ref, GetFunctionName(function), out_module.function_refs[i], out_error))
            {
                UnloadModule(out_module);
                return false;
            }
        }

        out_error.clear();
        return true;
    }

    bool LuaScriptRuntime::LoadFunctionRef(int module_ref, const char* function_name, int& out_function_ref, String& out_error)
    {
        out_function_ref = lua_no_ref;
        lua_rawgeti(lua_state, LUA_REGISTRYINDEX, module_ref); // pushes the module table from the registry onto the stack
        lua_getfield(lua_state, -1, function_name); // pushes module[function_name] onto the stack
        if (lua_isnil(lua_state, -1))
        {
            lua_pop(lua_state, 2); // removes the nil function value and module table from the stack
            out_error.clear();
            return true;
        }

        if (!lua_isfunction(lua_state, -1))
        {
            lua_pop(lua_state, 2); // removes the invalid function value and module table from the stack
            out_error = String(function_name) + " is not a function.";
            return false;
        }

        out_function_ref = luaL_ref(lua_state, LUA_REGISTRYINDEX); // stores the function in the registry and pops it from the stack
        lua_pop(lua_state, 1); // removes the module table from the stack
        out_error.clear();
        return true;
    }

    void LuaScriptRuntime::UnloadModule(LuaScriptModule& module)
    {
        if (!lua_state)
        {
            return;
        }

        if (module.module_ref != lua_no_ref)
        {
            luaL_unref(lua_state, LUA_REGISTRYINDEX, module.module_ref); // releases the registry reference so Lua can collect it
            module.module_ref = lua_no_ref;
        }

        for (uint32 i = 0; i < static_cast<uint32>(LuaScriptFunction::Count); ++i)
        {
            if (module.function_refs[i] != lua_no_ref)
            {
                luaL_unref(lua_state, LUA_REGISTRYINDEX, module.function_refs[i]); // releases the cached function reference
                module.function_refs[i] = lua_no_ref;
            }
        }
    }

    bool LuaScriptRuntime::CallFunction(ScriptInstanceHandle handle, LuaScriptFunction function, const ScriptCallContext& context, float delta_time, bool has_delta_time, String& out_error)
    {
        if (!lua_state)
        {
            out_error = "Lua state is not initialized.";
            return false;
        }

        if (!handle.IsValid())
        {
            out_error = "Script instance was not found.";
            return false;
        }

        auto instance_it = instances.find(handle.value);
        if (instance_it == instances.end())
        {
            out_error = "Script instance was not found.";
            return false;
        }

        auto module_it = modules.find(instance_it->second.script_path);
        if (module_it == modules.end())
        {
            out_error = "Script module was not found.";
            return false;
        }

        const int function_ref = module_it->second.function_refs[GetFunctionIndex(function)];
        if (function_ref == lua_no_ref)
        {
            out_error.clear();
            return true;
        }

        LuaStackGuard stack_guard(lua_state);
        lua_rawgeti(lua_state, LUA_REGISTRYINDEX, function_ref); // pushes the cached script function onto the stack       stack: [function]
        lua_rawgeti(lua_state, LUA_REGISTRYINDEX, instance_it->second.self_ref); // pushes the self table used as the first script argument       stack: [function, self]
        lua_pushinteger(lua_state, static_cast<lua_Integer>(context.entity)); // pushes the entity_id       stack: [function, self, entity_id]
        lua_setfield(lua_state, -2, "entity"); // set self's "entity" field and pops the pushed entity_id       stack: [function, self.entity]

        int arg_count = 1;
        if (has_delta_time)
        {
            lua_pushnumber(lua_state, static_cast<lua_Number>(delta_time)); // pushes dt as the second script argument
            arg_count = 2;
        }

        ScriptCallContext previous_context = current_context;
        current_context = context;
        if (lua_pcall(lua_state, arg_count, 0, 0) != LUA_OK) // calls the script function and consumes the function plus its arguments
        {
            const char* error = lua_tostring(lua_state, -1);
            out_error = error ? error : String("Failed to call ") + GetFunctionName(function) + ".";
            current_context = previous_context;
            return false;
        }

        current_context = previous_context;
        out_error.clear();
        return true;
    }
}
