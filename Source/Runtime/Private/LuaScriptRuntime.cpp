#include "LuaScriptRuntime.h"

#include "Backlog.h"
#include "ProjectSettings.h"
#include "UserSettings.h"
#include "EventHandler.h"
#include "GameData.h"
#include "Input.h"
#include "ProjectSettings.h"
#include "Scene.h"
#include "Localization.h"
#include "SceneManager.h"
#include "View.h"
#include "SceneComponents.h"
#include "Sound.h"
#include "WaterSurface.h"
#include "ShaderInterop_Renderer.h"

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
        constexpr Size max_behavior_tree_nodes = 4096;

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

        const char* GetFunctionName(ScriptCallType type)
        {
            switch (type)
            {
            case ScriptCallType::OnCreate:
                return "OnCreate";
            case ScriptCallType::OnUpdate:
                return "OnUpdate";
            case ScriptCallType::OnDestroy:
                return "OnDestroy";
            case ScriptCallType::OnTriggerEnter3D:
                return "OnTriggerEnter3D";
            case ScriptCallType::OnTriggerStay3D:
                return "OnTriggerStay3D";
            case ScriptCallType::OnTriggerExit3D:
                return "OnTriggerExit3D";
            case ScriptCallType::OnClick:
                return "OnClick";
            case ScriptCallType::OnAnimationEvent:
                return "OnAnimationEvent";
            case ScriptCallType::OnSequenceEvent:
                return "OnSequenceEvent";
            case ScriptCallType::OnBehaviorAction:
                return "OnBehaviorAction";
            case ScriptCallType::OnBehaviorAbort:
                return "OnBehaviorAbort";
            default:
                return "";
            }
        }

        uint32 GetFunctionIndex(ScriptCallType type)
        {
            return static_cast<uint32>(type);
        }

        void PushValue(lua_State* state, const won::function::Value& v)
        {
            switch (v.type)
            {
            case won::ValueType::Bool:    lua_pushboolean(state, v.bool_value);                         break;
            case won::ValueType::Int32:   lua_pushinteger(state, v.int32_value);                        break;
            case won::ValueType::Int64:   lua_pushinteger(state, static_cast<lua_Integer>(v.int64_value)); break;
            case won::ValueType::Float32: lua_pushnumber(state, v.float_value);                         break;
            case won::ValueType::Float64: lua_pushnumber(state, v.double_value);                        break;
            case won::ValueType::String:  lua_pushstring(state, v.string_value ? v.string_value : ""); break;
            default:                      lua_pushnil(state);                                           break;
            }
        }

        won::function::Value ToValue(lua_State* state, int index)
        {
            won::function::Value v;
            if (lua_isboolean(state, index))
            {
                v.type = won::ValueType::Bool;
                v.bool_value = lua_toboolean(state, index) != 0;
            }
            else if (lua_isinteger(state, index))
            {
                v.type = won::ValueType::Int64;
                v.int64_value = lua_tointeger(state, index);
            }
            else if (lua_isnumber(state, index))
            {
                v.type = won::ValueType::Float64;
                v.double_value = lua_tonumber(state, index);
            }
            else if (lua_isstring(state, index))
            {
                v.type = won::ValueType::String;
                v.string_value = lua_tostring(state, index);
            }
            return v;
        }
    }

    LuaScriptRuntime::LuaScriptRuntime(const ScriptRuntimeDesc& desc)
    {
        game_data = desc.game_data;
        user_settings = desc.user_settings;
        project_settings = desc.project_settings;
        apply_user_settings = desc.apply_user_settings;
        save_user_settings = desc.save_user_settings;
        audio_mixer = desc.audio_mixer;
        scene_manager = desc.scene_manager;
        content_root = desc.content_root;
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
        won::backlog::Post("[LuaScriptRuntime] instance created: " + desc.script_path);

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

    int LuaScriptRuntime::LuaTransformGetForward(lua_State* state)
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

        const float4& q = transform->rotation;
        lua_pushnumber(state, 2.0f * (q.x * q.z + q.w * q.y));
        lua_pushnumber(state, 2.0f * (q.y * q.z - q.w * q.x));
        lua_pushnumber(state, 1.0f - 2.0f * (q.x * q.x + q.y * q.y));
        return 3;
    }

    int LuaScriptRuntime::LuaTransformGetRotation(lua_State* state)
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

        lua_pushnumber(state, transform->rotation.x);
        lua_pushnumber(state, transform->rotation.y);
        lua_pushnumber(state, transform->rotation.z);
        lua_pushnumber(state, transform->rotation.w);
        return 4;
    }

    int LuaScriptRuntime::LuaTransformSetRotation(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 5 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::TransformComponent* transform = runtime->current_context.scene->GetComponent<ecs::TransformComponent>(entity);
        if (!transform)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        transform->rotation = float4(
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2)),
            static_cast<float>(luaL_checknumber(state, value_index + 3)));
        transform->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaTransformSetEuler(lua_State* state)
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

        transform->SetRotationEuler(
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2)));
        lua_pushboolean(state, true);
        return 1;
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

    int LuaScriptRuntime::LuaMaterialHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialGetBaseColor(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_entity_arg && arg_count >= 2 ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushnil(state);
            return 1;
        }

        const float4& base_color = material->GetMaterialSlot(slot_index).base_color;
        lua_pushnumber(state, base_color.x);
        lua_pushnumber(state, base_color.y);
        lua_pushnumber(state, base_color.z);
        lua_pushnumber(state, base_color.w);
        return 4;
    }

    int LuaScriptRuntime::LuaMaterialSetBaseColor(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 5 && lua_isinteger(state, 1);
        const bool has_slot_arg = has_entity_arg && arg_count >= 6 && lua_isinteger(state, 2);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_slot_arg ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        const int value_index = has_slot_arg ? 3 : (has_entity_arg ? 2 : 1);
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushboolean(state, false);
            return 1;
        }

        resource::MaterialSlot& material_slot = material->GetMaterialSlot(slot_index);
        material_slot.base_color.x = static_cast<float>(luaL_checknumber(state, value_index));
        material_slot.base_color.y = static_cast<float>(luaL_checknumber(state, value_index + 1));
        material_slot.base_color.z = static_cast<float>(luaL_checknumber(state, value_index + 2));
        material_slot.base_color.w = static_cast<float>(luaL_optnumber(state, value_index + 3, 1.0));
        material->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialGetRoughness(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_entity_arg && arg_count >= 2 ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, material->GetMaterialSlot(slot_index).roughness);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialSetRoughness(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 2 && lua_isinteger(state, 1);
        const bool has_slot_arg = has_entity_arg && arg_count >= 3 && lua_isinteger(state, 2);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_slot_arg ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        const int value_index = has_slot_arg ? 3 : (has_entity_arg ? 2 : 1);
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushboolean(state, false);
            return 1;
        }

        material->GetMaterialSlot(slot_index).roughness = static_cast<float>(luaL_checknumber(state, value_index));
        material->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialGetMetallic(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_entity_arg && arg_count >= 2 ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, material->GetMaterialSlot(slot_index).metallic);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialSetMetallic(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 2 && lua_isinteger(state, 1);
        const bool has_slot_arg = has_entity_arg && arg_count >= 3 && lua_isinteger(state, 2);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const uint32 slot_index = has_slot_arg ? static_cast<uint32>(luaL_checkinteger(state, 2)) : 0u;
        const int value_index = has_slot_arg ? 3 : (has_entity_arg ? 2 : 1);
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material || slot_index >= material->GetMaterialSlotCount())
        {
            lua_pushboolean(state, false);
            return 1;
        }

        material->GetMaterialSlot(slot_index).metallic = static_cast<float>(luaL_checknumber(state, value_index));
        material->SetDirty();
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

    int LuaScriptRuntime::LuaInputIsActionDown(lua_State* state)
    {
        lua_pushboolean(state, io::IsActionDown(luaL_checkstring(state, 1)));
        return 1;
    }

    int LuaScriptRuntime::LuaInputIsActionPressed(lua_State* state)
    {
        lua_pushboolean(state, io::IsActionPressed(luaL_checkstring(state, 1)));
        return 1;
    }

    int LuaScriptRuntime::LuaInputGetActionValue(lua_State* state)
    {
        lua_pushnumber(state, static_cast<lua_Number>(io::GetActionValue(luaL_checkstring(state, 1))));
        return 1;
    }

    int LuaScriptRuntime::LuaInputGetActionAxis2D(lua_State* state)
    {
        const float2 axis = io::GetActionAxis2D(luaL_checkstring(state, 1));
        lua_pushnumber(state, static_cast<lua_Number>(axis.x));
        lua_pushnumber(state, static_cast<lua_Number>(axis.y));
        return 2;
    }

    int LuaScriptRuntime::LuaInputGetGamepadAxis(lua_State* state)
    {
        const StringView axis = luaL_checkstring(state, 1);
        const io::GamepadState* gamepad = io::GetGamepadState(0);
        float value = 0.0f;
        if (gamepad && gamepad->connected)
        {
            if (axis == "left_x") value = gamepad->left_stick.x;
            else if (axis == "left_y") value = gamepad->left_stick.y;
            else if (axis == "right_x") value = gamepad->right_stick.x;
            else if (axis == "right_y") value = gamepad->right_stick.y;
            else if (axis == "left_trigger") value = gamepad->left_trigger;
            else if (axis == "right_trigger") value = gamepad->right_trigger;
        }
        lua_pushnumber(state, static_cast<lua_Number>(value));
        return 1;
    }

    int LuaScriptRuntime::LuaInputIsGamepadConnected(lua_State* state)
    {
        const io::GamepadState* gamepad = io::GetGamepadState(0);
        lua_pushboolean(state, gamepad != nullptr && gamepad->connected);
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

    int LuaScriptRuntime::LuaSceneLoad(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* path = luaL_checkstring(state, 1);
        if (runtime && runtime->scene_manager && runtime->current_context.scene)
        {
            runtime->scene_manager->QueueSceneLoad(*runtime->current_context.scene, path);
        }
        return 0;
    }

    int LuaScriptRuntime::LuaSceneLoadAdditive(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* path = luaL_checkstring(state, 1);
        if (!runtime || !runtime->scene_manager || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        String error;
        const ecs::Entity root = runtime->scene_manager->LoadSceneAdditive(*runtime->current_context.scene, path, &error);
        if (root == ecs::INVALID_ENTITY)
        {
            backlog::Post("won.scene.load_additive failed: " + String(path) + (error.empty() ? String() : " (" + error + ")"), backlog::LogLevel::Error);
            lua_pushnil(state);
            return 1;
        }

        lua_pushinteger(state, static_cast<lua_Integer>(root));
        return 1;
    }

    int LuaScriptRuntime::LuaSceneUnloadAdditive(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const ecs::Entity root = static_cast<ecs::Entity>(luaL_checkinteger(state, 1));
        bool unloaded = false;
        if (runtime && runtime->scene_manager && runtime->current_context.scene)
        {
            unloaded = runtime->scene_manager->UnloadSceneAdditive(*runtime->current_context.scene, root);
        }
        lua_pushboolean(state, unloaded ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaSceneIsLoading(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const bool loading = runtime && runtime->scene_manager && runtime->current_context.scene &&
            runtime->scene_manager->IsLoading(runtime->current_context.scene);
        lua_pushboolean(state, loading ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaNavFindPath(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        nav::NavMesh* nav_mesh = (runtime && runtime->current_context.scene) ? runtime->current_context.scene->GetNavMesh() : nullptr;
        if (!nav_mesh)
        {
            lua_pushnil(state);
            return 1;
        }

        const float3 start = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        const float3 end = {
            static_cast<float>(luaL_checknumber(state, 4)),
            static_cast<float>(luaL_checknumber(state, 5)),
            static_cast<float>(luaL_checknumber(state, 6))
        };

        Vector<float3> path;
        if (!nav_mesh->FindPath(start, end, path))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_createtable(state, static_cast<int>(path.size()), 0);
        for (Size i = 0; i < path.size(); ++i)
        {
            lua_createtable(state, 0, 3);
            lua_pushnumber(state, path[i].x); lua_setfield(state, -2, "x");
            lua_pushnumber(state, path[i].y); lua_setfield(state, -2, "y");
            lua_pushnumber(state, path[i].z); lua_setfield(state, -2, "z");
            lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    }

    int LuaScriptRuntime::LuaNavNearestPoint(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        nav::NavMesh* nav_mesh = (runtime && runtime->current_context.scene) ? runtime->current_context.scene->GetNavMesh() : nullptr;
        if (!nav_mesh)
        {
            lua_pushnil(state);
            return 1;
        }

        const float3 position = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        float3 nearest = {};
        if (!nav_mesh->FindNearestPoint(position, nearest))
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, nearest.x);
        lua_pushnumber(state, nearest.y);
        lua_pushnumber(state, nearest.z);
        return 3;
    }

    int LuaScriptRuntime::LuaNavIsReady(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        nav::NavMesh* nav_mesh = (runtime && runtime->current_context.scene) ? runtime->current_context.scene->GetNavMesh() : nullptr;
        lua_pushboolean(state, (nav_mesh && nav_mesh->IsValid()) ? 1 : 0);
        return 1;
    }

    ecs::BehaviorTreeComponent* LuaScriptRuntime::GetSelfBehaviorTree(LuaScriptRuntime* runtime)
    {
        if (!runtime || !runtime->current_context.scene)
        {
            return nullptr;
        }
        return runtime->current_context.scene->GetComponent<ecs::BehaviorTreeComponent>(runtime->current_context.entity);
    }

    int LuaScriptRuntime::LuaAIAddBehaviorTree(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::BehaviorTreeComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaWaterAddRipple(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        ecs::Scene& scene = *runtime->current_context.scene;

        ecs::WaterRippleRequest request = {};
        request.position = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        request.strength = static_cast<float>(luaL_optnumber(state, 4, 1.0));
        scene.QueueWaterRipple(request);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaWaterSampleHeight(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const float2 world_xz = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2))
        };
        const water::SurfaceSample sample = water::SampleSurface(*runtime->current_context.scene, world_xz);
        if (!sample.valid)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, sample.height);
        return 1;
    }

    int LuaScriptRuntime::LuaWaterSampleSurface(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const float2 world_xz = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2))
        };
        const water::SurfaceSample sample = water::SampleSurface(*runtime->current_context.scene, world_xz);
        if (!sample.valid)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, sample.height);
        lua_pushnumber(state, sample.normal.x);
        lua_pushnumber(state, sample.normal.y);
        lua_pushnumber(state, sample.normal.z);
        lua_pushnumber(state, sample.velocity.x);
        lua_pushnumber(state, sample.velocity.y);
        lua_pushnumber(state, sample.velocity.z);
        return 7;
    }

    int LuaScriptRuntime::LuaAISetTree(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        luaL_checktype(state, 1, LUA_TTABLE);

        ecs::BehaviorTreeComponent* component = GetSelfBehaviorTree(runtime);
        if (!component)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        Vector<ai::BehaviorTreeNode> nodes;
        nodes.resize(1);
        Vector<int32> node_refs;
        lua_pushvalue(state, 1);
        node_refs.push_back(luaL_ref(state, LUA_REGISTRYINDEX));

        String error;
        for (Size i = 0; i < node_refs.size() && error.empty(); ++i)
        {
            lua_rawgeti(state, LUA_REGISTRYINDEX, node_refs[i]);
            ai::BehaviorTreeNode node = {};

            lua_getfield(state, -1, "type");
            const char* type_name = lua_tostring(state, -1);
            if (!type_name)
            {
                error = "node is missing 'type'";
            }
            else if (std::strcmp(type_name, "selector") == 0)
            {
                node.type = ai::BehaviorTreeNode::Type::Selector;
            }
            else if (std::strcmp(type_name, "sequence") == 0)
            {
                node.type = ai::BehaviorTreeNode::Type::Sequence;
            }
            else if (std::strcmp(type_name, "action") == 0)
            {
                node.type = ai::BehaviorTreeNode::Type::Action;
            }
            else if (std::strcmp(type_name, "condition") == 0)
            {
                node.type = ai::BehaviorTreeNode::Type::Condition;
            }
            else
            {
                error = String("unknown node type: ") + type_name;
            }
            lua_pop(state, 1);

            lua_getfield(state, -1, "name");
            if (const char* node_name = lua_tostring(state, -1))
            {
                node.name = node_name;
            }
            lua_pop(state, 1);

            lua_getfield(state, -1, "key");
            if (const char* key = lua_tostring(state, -1))
            {
                node.operand.key = key;
            }
            lua_pop(state, 1);

            lua_getfield(state, -1, "op");
            if (const char* op_name = lua_tostring(state, -1))
            {
                if (std::strcmp(op_name, "notequal") == 0)
                {
                    node.op = ai::BehaviorTreeNode::CompareOp::NotEqual;
                }
                else if (std::strcmp(op_name, "less") == 0)
                {
                    node.op = ai::BehaviorTreeNode::CompareOp::Less;
                }
                else if (std::strcmp(op_name, "greater") == 0)
                {
                    node.op = ai::BehaviorTreeNode::CompareOp::Greater;
                }
                else
                {
                    node.op = ai::BehaviorTreeNode::CompareOp::Equal;
                }
            }
            lua_pop(state, 1);

            lua_getfield(state, -1, "value");
            switch (lua_type(state, -1))
            {
            case LUA_TBOOLEAN:
                node.operand.value.type = ai::Blackboard::Value::Type::Bool;
                node.operand.value.bool_value = lua_toboolean(state, -1) != 0;
                break;
            case LUA_TNUMBER:
                node.operand.value.type = ai::Blackboard::Value::Type::Float;
                node.operand.value.float_value = static_cast<float>(lua_tonumber(state, -1));
                break;
            case LUA_TSTRING:
                node.operand.value.type = ai::Blackboard::Value::Type::String;
                node.operand.value.string_value = lua_tostring(state, -1);
                break;
            default:
                break;
            }
            lua_pop(state, 1);

            lua_getfield(state, -1, "children");
            const int32 child_count = lua_istable(state, -1) ? static_cast<int32>(lua_rawlen(state, -1)) : 0;
            const int32 first_child = static_cast<int32>(nodes.size());
            node.first_child = child_count > 0 ? first_child : -1;
            node.child_count = child_count;
            nodes[i] = node;

            if (static_cast<Size>(first_child) + static_cast<Size>(child_count) > max_behavior_tree_nodes)
            {
                error = "tree exceeds the maximum node count";
            }
            else if (child_count > 0)
            {
                nodes.resize(static_cast<Size>(first_child) + static_cast<Size>(child_count));
                for (int32 child = 0; child < child_count; ++child)
                {
                    lua_rawgeti(state, -1, child + 1);
                    if (!lua_istable(state, -1))
                    {
                        error = "child entry is not a table";
                        lua_pop(state, 1);
                        break;
                    }
                    node_refs.push_back(luaL_ref(state, LUA_REGISTRYINDEX));
                }
            }
            lua_pop(state, 2);
        }

        for (int32 node_ref : node_refs)
        {
            luaL_unref(state, LUA_REGISTRYINDEX, node_ref);
        }

        if (error.empty())
        {
            std::shared_ptr<ai::BehaviorTree> tree = std::make_shared<ai::BehaviorTree>();
            tree->nodes = std::move(nodes);
            if (ai::Validate(*tree, error))
            {
                component->tree = tree;
                component->node_cursor.clear();
                component->running_action_node = -1;
                component->pending_action_node = -1;
                component->pending_abort_node = -1;
                component->action_result_node = -1;
                component->missing_key_warned = false;
                lua_pushboolean(state, 1);
                return 1;
            }
        }

        backlog::Post("[won.ai] set_tree failed: " + error, backlog::LogLevel::Error);
        lua_pushboolean(state, 0);
        return 1;
    }

    int LuaScriptRuntime::LuaAISet(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* key = luaL_checkstring(state, 1);

        ai::Blackboard::Value value = {};
        switch (lua_type(state, 2))
        {
        case LUA_TBOOLEAN:
            value.type = ai::Blackboard::Value::Type::Bool;
            value.bool_value = lua_toboolean(state, 2) != 0;
            break;
        case LUA_TNUMBER:
            value.type = ai::Blackboard::Value::Type::Float;
            value.float_value = static_cast<float>(lua_tonumber(state, 2));
            break;
        case LUA_TSTRING:
            value.type = ai::Blackboard::Value::Type::String;
            value.string_value = lua_tostring(state, 2);
            break;
        default:
            lua_pushboolean(state, 0);
            return 1;
        }

        ecs::BehaviorTreeComponent* component = GetSelfBehaviorTree(runtime);
        if (!component)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        for (ai::Blackboard& entry : component->blackboard)
        {
            if (entry.key == key)
            {
                entry.value = value;
                lua_pushboolean(state, 1);
                return 1;
            }
        }

        ai::Blackboard entry = {};
        entry.key = key;
        entry.value = value;
        component->blackboard.push_back(entry);
        lua_pushboolean(state, 1);
        return 1;
    }

    int LuaScriptRuntime::LuaAIGet(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* key = luaL_checkstring(state, 1);

        ecs::BehaviorTreeComponent* component = GetSelfBehaviorTree(runtime);
        if (component)
        {
            for (const ai::Blackboard& entry : component->blackboard)
            {
                if (entry.key != key)
                {
                    continue;
                }
                switch (entry.value.type)
                {
                case ai::Blackboard::Value::Type::Bool:
                    lua_pushboolean(state, entry.value.bool_value ? 1 : 0);
                    return 1;
                case ai::Blackboard::Value::Type::Float:
                    lua_pushnumber(state, entry.value.float_value);
                    return 1;
                case ai::Blackboard::Value::Type::String:
                    lua_pushstring(state, entry.value.string_value.c_str());
                    return 1;
                default:
                    break;
                }
                break;
            }
        }

        lua_pushnil(state);
        return 1;
    }

    ecs::NavAgentComponent* LuaScriptRuntime::GetNavAgent(LuaScriptRuntime* runtime, ecs::Entity entity)
    {
        if (!runtime || !runtime->current_context.scene)
        {
            return nullptr;
        }
        return runtime->current_context.scene->GetComponent<ecs::NavAgentComponent>(entity);
    }

    int LuaScriptRuntime::LuaAIHasBehaviorTree(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::BehaviorTreeComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAIHasNavAgent(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::NavAgentComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAIAddNavAgent(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::NavAgentComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    static bool StartNavAgentMove(ecs::Scene& scene, ecs::Entity entity, ecs::NavAgentComponent& agent, const float3& target)
    {
        agent.path.clear();
        agent.path_index = 0;

        ecs::TransformComponent* transform = scene.GetComponent<ecs::TransformComponent>(entity);
        nav::NavMesh* nav_mesh = scene.GetNavMesh();
        if (!transform || !nav_mesh || !nav_mesh->IsValid())
        {
            agent.state = ecs::NavAgentComponent::MoveState::Failed;
            return false;
        }

        float3 goal = target;
        nav_mesh->FindNearestPoint(target, goal);
        if (!nav_mesh->FindPath(transform->position, goal, agent.path) || agent.path.size() < 2)
        {
            agent.path.clear();
            agent.state = ecs::NavAgentComponent::MoveState::Failed;
            return false;
        }

        agent.move_target = goal;
        agent.path_index = 1;
        agent.state = ecs::NavAgentComponent::MoveState::Moving;
        return true;
    }

    int LuaScriptRuntime::LuaAIMoveTo(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 4 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;

        const float3 target = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };

        ecs::NavAgentComponent* agent = GetNavAgent(runtime, entity);
        if (!agent)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        lua_pushboolean(state, StartNavAgentMove(*runtime->current_context.scene, entity, *agent, target) ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaAIStop(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const bool has_entity_arg = lua_gettop(state) >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1))
            : (runtime ? runtime->current_context.entity : 0);

        ecs::NavAgentComponent* agent = GetNavAgent(runtime, entity);
        if (!agent)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        agent->path.clear();
        agent->path_index = 0;
        agent->state = ecs::NavAgentComponent::MoveState::Idle;
        lua_pushboolean(state, 1);
        return 1;
    }

    int LuaScriptRuntime::LuaAIGetMoveState(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const bool has_entity_arg = lua_gettop(state) >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1))
            : (runtime ? runtime->current_context.entity : 0);

        ecs::NavAgentComponent* agent = GetNavAgent(runtime, entity);
        const ecs::NavAgentComponent::MoveState move_state = agent ? agent->state : ecs::NavAgentComponent::MoveState::Idle;
        lua_pushinteger(state, static_cast<lua_Integer>(move_state));
        return 1;
    }

    int LuaScriptRuntime::LuaAISetMoveSpeed(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 2 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1))
            : (runtime ? runtime->current_context.entity : 0);
        const int value_index = has_entity_arg ? 2 : 1;
        const float speed = static_cast<float>(luaL_checknumber(state, value_index));

        ecs::NavAgentComponent* agent = GetNavAgent(runtime, entity);
        if (!agent)
        {
            lua_pushboolean(state, 0);
            return 1;
        }
        agent->move_speed = speed;
        lua_pushboolean(state, 1);
        return 1;
    }

    int LuaScriptRuntime::LuaLocaleGetText(lua_State* state)
    {
        const String key = luaL_checkstring(state, 1);
        const String value = locale::GetText(key);
        lua_pushstring(state, value.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaLocaleSetLanguage(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const String code = luaL_checkstring(state, 1);
        if (!locale::SetLanguage(code))
        {
            lua_pushboolean(state, 0);
            return 1;
        }
        if (runtime && runtime->user_settings)
        {
            runtime->user_settings->language = code;
        }
        lua_pushboolean(state, 1);
        return 1;
    }

    int LuaScriptRuntime::LuaLocaleGetLanguage(lua_State* state)
    {
        lua_pushstring(state, locale::GetLanguage().c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaLocaleGetAvailableLanguages(lua_State* state)
    {
        const Vector<String>& languages = locale::GetAvailableLanguages();
        lua_createtable(state, static_cast<int>(languages.size()), 0);
        for (Size index = 0; index < languages.size(); ++index)
        {
            lua_pushstring(state, languages[index].c_str());
            lua_rawseti(state, -2, static_cast<lua_Integer>(index + 1));
        }
        return 1;
    }

    int LuaScriptRuntime::LuaSettingsGet(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const String key = luaL_checkstring(state, 1);
        if (!runtime || !runtime->user_settings || !runtime->project_settings)
        {
            lua_pushnil(state);
            return 1;
        }

        const settings::UserSettings& user = *runtime->user_settings;
        const project::ProjectSettings& project = *runtime->project_settings;
        if (key == "vsync")
        {
            lua_pushboolean(state, user.vsync.value_or(project.vsync_enabled) ? 1 : 0);
            return 1;
        }
        if (key == "aa_mode")
        {
            const rendering::AntiAliasingMode aa_mode = user.aa_mode.value_or(project.aa_mode);
            lua_pushstring(state, aa_mode == rendering::AntiAliasingMode::FXAA ? "FXAA" : "None");
            return 1;
        }
        if (key == "shadow_resolution_scale")
        {
            lua_pushnumber(state, static_cast<lua_Number>(user.shadow_resolution_scale.value_or(1.0f)));
            return 1;
        }
        lua_pushnil(state);
        return 1;
    }

    int LuaScriptRuntime::LuaSettingsSet(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const String key = luaL_checkstring(state, 1);
        if (!runtime || !runtime->user_settings)
        {
            lua_pushboolean(state, 0);
            return 1;
        }

        settings::UserSettings& user = *runtime->user_settings;
        bool applied = false;
        if (key == "vsync")
        {
            user.vsync = lua_toboolean(state, 2) != 0;
            applied = true;
        }
        else if (key == "aa_mode")
        {
            const String value = luaL_checkstring(state, 2);
            if (value == "FXAA" || value == "None")
            {
                user.aa_mode = value == "FXAA" ? rendering::AntiAliasingMode::FXAA : rendering::AntiAliasingMode::None;
                applied = true;
            }
        }
        else if (key == "shadow_resolution_scale")
        {
            user.shadow_resolution_scale = static_cast<float>(luaL_checknumber(state, 2));
            applied = true;
        }

        if (applied && runtime->apply_user_settings)
        {
            runtime->apply_user_settings();
        }
        lua_pushboolean(state, applied ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaSettingsSave(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const bool saved = runtime && runtime->save_user_settings && runtime->save_user_settings();
        lua_pushboolean(state, saved ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaText2DSetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const bool has_entity_arg = lua_gettop(state) >= 2 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const char* text = luaL_checkstring(state, has_entity_arg ? 2 : 1);
        ecs::Text2DComponent* component = runtime->current_context.scene->GetComponent<ecs::Text2DComponent>(entity);
        if (component)
        {
            component->text = text;
            component->SetDirty();
        }
        lua_pushboolean(state, component != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaText2DGetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : (runtime ? runtime->current_context.entity : ecs::INVALID_ENTITY);
        ecs::Text2DComponent* component = (runtime && runtime->current_context.scene) ? runtime->current_context.scene->GetComponent<ecs::Text2DComponent>(entity) : nullptr;
        if (!component)
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushstring(state, component->text.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaText3DSetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const bool has_entity_arg = lua_gettop(state) >= 2 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const char* text = luaL_checkstring(state, has_entity_arg ? 2 : 1);
        ecs::Text3DComponent* component = runtime->current_context.scene->GetComponent<ecs::Text3DComponent>(entity);
        if (component)
        {
            component->text = text;
            component->SetDirty();
        }
        lua_pushboolean(state, component != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaText3DGetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : (runtime ? runtime->current_context.entity : ecs::INVALID_ENTITY);
        ecs::Text3DComponent* component = (runtime && runtime->current_context.scene) ? runtime->current_context.scene->GetComponent<ecs::Text3DComponent>(entity) : nullptr;
        if (!component)
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushstring(state, component->text.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaEventSubscribe(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        luaL_checktype(state, 2, LUA_TFUNCTION);

        const uint64 id = won::eventhandler::HashEvent(name);
        const int lua_ref = luaL_ref(state, LUA_REGISTRYINDEX);

        eventhandler::Handle handle = eventhandler::Subscribe(id,
            [runtime, lua_ref](const won::function::Value& payload)
            {
                lua_rawgeti(runtime->lua_state, LUA_REGISTRYINDEX, lua_ref);
                PushValue(runtime->lua_state, payload);
                lua_pcall(runtime->lua_state, 1, 0, 0);
            });

        runtime->event_handles.push_back(std::move(handle));
        return 0;
    }

    int LuaScriptRuntime::LuaEventPost(lua_State* state)
    {
        const char* name = luaL_checkstring(state, 1);
        won::function::Value payload;
        if (lua_gettop(state) >= 2)
            payload = ToValue(state, 2);
        eventhandler::PostEvent(won::eventhandler::HashEvent(name), payload);
        return 0;
    }

    int LuaScriptRuntime::LuaEventFire(lua_State* state)
    {
        const char* name = luaL_checkstring(state, 1);
        won::function::Value payload;
        if (lua_gettop(state) >= 2)
            payload = ToValue(state, 2);
        eventhandler::FireEvent(won::eventhandler::HashEvent(name), payload);
        return 0;
    }

    int LuaScriptRuntime::LuaGameDataGetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushnil(state);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        const char* value = runtime->game_data->GetString(key);
        if (!value)
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushstring(state, value);
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataSetString(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        const char* value = luaL_checkstring(state, 2);
        lua_pushboolean(state, runtime->game_data->SetString(key, value) ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataGetInt(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushnil(state);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        int value = 0;
        if (!runtime->game_data->GetInt(key, value))
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushinteger(state, static_cast<lua_Integer>(value));
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataSetInt(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        const int value = static_cast<int>(luaL_checkinteger(state, 2));
        lua_pushboolean(state, runtime->game_data->SetInt(key, value) ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataGetFloat(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushnil(state);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        float value = 0.0f;
        if (!runtime->game_data->GetFloat(key, value))
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, static_cast<lua_Number>(value));
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataSetFloat(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        const float value = static_cast<float>(luaL_checknumber(state, 2));
        lua_pushboolean(state, runtime->game_data->SetFloat(key, value) ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataGetBool(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushnil(state);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        bool value = false;
        if (!runtime->game_data->GetBool(key, value))
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushboolean(state, value ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaGameDataSetBool(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->game_data)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const char* key = luaL_checkstring(state, 1);
        const bool value = lua_toboolean(state, 2) != 0;
        lua_pushboolean(state, runtime->game_data->SetBool(key, value) ? 1 : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaEntityCreate(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = runtime->current_context.scene->CreateEntity();
        lua_pushinteger(state, static_cast<lua_Integer>(entity));
        return 1;
    }

    int LuaScriptRuntime::LuaEntitySpawnPrefab(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const char* path = luaL_checkstring(state, 1);
        const float x = static_cast<float>(luaL_checknumber(state, 2));
        const float y = static_cast<float>(luaL_checknumber(state, 3));
        const float z = static_cast<float>(luaL_checknumber(state, 4));
        const float yaw = lua_gettop(state) >= 5 ? static_cast<float>(luaL_checknumber(state, 5)) : 0.0f;

        const ecs::Entity root = runtime->current_context.scene->CreateEntity();
        ecs::PrefabSpawnRequest request;
        request.path = path;
        request.position = { x, y, z };
        request.yaw = yaw;
        request.reserved_root = root;
        runtime->current_context.scene->QueuePrefabSpawn(request);

        lua_pushinteger(state, static_cast<lua_Integer>(root));
        return 1;
    }

    int LuaScriptRuntime::LuaEntitySpawnPrefabChild(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity parent = static_cast<ecs::Entity>(luaL_checkinteger(state, 1));
        const char* path = luaL_checkstring(state, 2);
        const float x = static_cast<float>(luaL_checknumber(state, 3));
        const float y = static_cast<float>(luaL_checknumber(state, 4));
        const float z = static_cast<float>(luaL_checknumber(state, 5));

        const ecs::Entity root = runtime->current_context.scene->CreateEntity();
        ecs::PrefabSpawnRequest request;
        request.path = path;
        request.position = { x, y, z };
        request.parent = parent;
        request.reserved_root = root;
        runtime->current_context.scene->QueuePrefabSpawn(request);

        lua_pushinteger(state, static_cast<lua_Integer>(root));
        return 1;
    }

    int LuaScriptRuntime::LuaEntityPreloadPrefab(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* path = luaL_checkstring(state, 1);
        if (runtime && runtime->scene_manager)
        {
            runtime->scene_manager->QueuePrefabPreload(path);
        }
        return 0;
    }

    int LuaScriptRuntime::LuaTransformAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::TransformComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::MaterialComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaMaterialFork(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::MaterialComponent* material = runtime->current_context.scene->GetComponent<ecs::MaterialComponent>(entity);
        if (!material)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        material->ForkMaterial();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->AddComponent<ecs::EnvironmentComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetActive(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->SetActive(lua_toboolean(state, value_index) != 0);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetSkyColors(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 7 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->sky_horizon_color = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        environment->sky_zenith_color = {
            static_cast<float>(luaL_checknumber(state, value_index + 3)),
            static_cast<float>(luaL_checknumber(state, value_index + 4)),
            static_cast<float>(luaL_checknumber(state, value_index + 5))
        };
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetAmbient(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 5 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->ambient_intensity = static_cast<float>(luaL_checknumber(state, value_index));
        environment->ambient_color = {
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2)),
            static_cast<float>(luaL_checknumber(state, value_index + 3))
        };
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetSunDirection(lua_State* state)
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
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->sun_direction = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetAtmosphere(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 3 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->turbidity = static_cast<float>(luaL_checknumber(state, value_index));
        environment->mie_eccentricity = static_cast<float>(luaL_checknumber(state, value_index + 1));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetSkyType(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->sky_type = static_cast<ecs::EnvironmentComponent::SkyType>(luaL_checkinteger(state, value_index));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetDiffuseGIMode(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->diffuse_gi_mode = static_cast<ecs::EnvironmentComponent::DiffuseGIMode>(luaL_checkinteger(state, value_index));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetReflectionMode(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->reflection_mode = static_cast<ecs::EnvironmentComponent::ReflectionMode>(luaL_checkinteger(state, value_index));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetDirectSunActive(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->direct_sun_active = lua_toboolean(state, value_index) != 0;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetCloud(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 3 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->cloud_coverage = static_cast<float>(luaL_checknumber(state, value_index));
        environment->cloud_density = static_cast<float>(luaL_checknumber(state, value_index + 1));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetCloudColor(lua_State* state)
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
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->cloud_color = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetCloudMotion(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 5 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->cloud_frequency = static_cast<float>(luaL_checknumber(state, value_index));
        environment->cloud_speed = static_cast<float>(luaL_checknumber(state, value_index + 1));
        environment->cloud_direction = {
            static_cast<float>(luaL_checknumber(state, value_index + 2)),
            static_cast<float>(luaL_checknumber(state, value_index + 3))
        };
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetScatteringCoefficients(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 3 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->rayleigh_coefficient = static_cast<float>(luaL_checknumber(state, value_index));
        environment->mie_coefficient = static_cast<float>(luaL_checknumber(state, value_index + 1));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaEnvironmentSetSunIntensity(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::EnvironmentComponent* environment = runtime->current_context.scene->GetComponent<ecs::EnvironmentComponent>(entity);
        if (!environment)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        environment->sun_intensity = static_cast<float>(luaL_checknumber(state, value_index));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaParticleEmitter3DHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::ParticleEmitter3DComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaParticleEmitter3DAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->AddComponent<ecs::ParticleEmitter3DComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaParticleEmitter3DSetActive(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::ParticleEmitter3DComponent* emitter = runtime->current_context.scene->GetComponent<ecs::ParticleEmitter3DComponent>(entity);
        if (!emitter)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        emitter->SetActive(lua_toboolean(state, value_index) != 0);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaParticleEmitter3DSetSpawnRate(lua_State* state)
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
        const int value_index = has_entity_arg ? 2 : 1;
        ecs::ParticleEmitter3DComponent* emitter = runtime->current_context.scene->GetComponent<ecs::ParticleEmitter3DComponent>(entity);
        if (!emitter)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        emitter->spawn_rate = static_cast<float>(luaL_checknumber(state, value_index));
        lua_pushboolean(state, true);
        return 1;
    }

    static void StartAnimationClip(ecs::AnimationComponent& animation, uint32 clip_index, float blend_duration)
    {
        if (clip_index >= animation.clips.size() || clip_index == animation.current_clip_index)
        {
            return;
        }
        if (blend_duration > 0.0f)
        {
            animation.prev_clip_index = animation.current_clip_index;
            animation.prev_time = animation.time;
            animation.blend_duration = blend_duration;
            animation.blend_elapsed = 0.0f;
            animation.blending = true;
        }
        else
        {
            animation.blending = false;
        }
        animation.current_clip_index = clip_index;
        animation.time = 0.0f;
        animation.event_scan_time = 0.0f;
        animation.playing = true;
        animation.bone_matrices_dirty = true;
    }

    ecs::AnimationComponent* LuaScriptRuntime::GetSelfAnimation(LuaScriptRuntime* runtime)
    {
        if (!runtime || !runtime->current_context.scene)
        {
            return nullptr;
        }
        return runtime->current_context.scene->GetComponent<ecs::AnimationComponent>(runtime->current_context.entity);
    }

    ecs::AnimationStateMachineComponent* LuaScriptRuntime::GetSelfStateMachine(LuaScriptRuntime* runtime)
    {
        if (!runtime || !runtime->current_context.scene)
        {
            return nullptr;
        }
        return runtime->current_context.scene->GetComponent<ecs::AnimationStateMachineComponent>(runtime->current_context.entity);
    }

    int LuaScriptRuntime::LuaAnimationSMAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->AddComponent<ecs::AnimationStateMachineComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::AnimationStateMachineComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSetBool(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        const bool value = lua_toboolean(state, 2) != 0;
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        for (ecs::AnimationParameter& parameter : state_machine->parameters)
        {
            if (parameter.name == name)
            {
                parameter.value = value ? 1.0f : 0.0f;
                lua_pushboolean(state, true);
                return 1;
            }
        }
        lua_pushboolean(state, false);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSetFloat(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        const float value = static_cast<float>(luaL_checknumber(state, 2));
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        for (ecs::AnimationParameter& parameter : state_machine->parameters)
        {
            if (parameter.name == name)
            {
                parameter.value = value;
                lua_pushboolean(state, true);
                return 1;
            }
        }
        lua_pushboolean(state, false);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSetTrigger(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        for (ecs::AnimationParameter& parameter : state_machine->parameters)
        {
            if (parameter.name == name)
            {
                parameter.value = 1.0f;
                lua_pushboolean(state, true);
                return 1;
            }
        }
        lua_pushboolean(state, false);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetState(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine || state_machine->current_state < 0 || state_machine->current_state >= static_cast<int32>(state_machine->states.size()))
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushstring(state, state_machine->states[state_machine->current_state].name.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMAddParameter(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        const char* type_name = luaL_checkstring(state, 2);
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        ecs::AnimationParameter parameter = {};
        parameter.name = name;
        if (std::strcmp(type_name, "bool") == 0)
        {
            parameter.type = ecs::AnimationParameter::Type::Bool;
        }
        else if (std::strcmp(type_name, "trigger") == 0)
        {
            parameter.type = ecs::AnimationParameter::Type::Trigger;
        }
        else
        {
            parameter.type = ecs::AnimationParameter::Type::Float;
        }
        state_machine->parameters.push_back(parameter);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMAddState(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        const char* clip = luaL_checkstring(state, 2);
        const bool loop = lua_gettop(state) >= 3 ? (lua_toboolean(state, 3) != 0) : true;
        const float speed = lua_gettop(state) >= 4 ? static_cast<float>(luaL_checknumber(state, 4)) : 1.0f;
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushinteger(state, -1);
            return 1;
        }
        ecs::AnimationState animation_state = {};
        animation_state.name = name;
        animation_state.clip = clip;
        animation_state.loop = loop;
        animation_state.speed = speed;
        state_machine->states.push_back(animation_state);
        lua_pushinteger(state, static_cast<lua_Integer>(state_machine->states.size() - 1));
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMAddTransition(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const int32 from_state = static_cast<int32>(luaL_checkinteger(state, 1));
        const int32 to_state = static_cast<int32>(luaL_checkinteger(state, 2));
        const float blend_duration = lua_gettop(state) >= 3 ? static_cast<float>(luaL_checknumber(state, 3)) : 0.2f;
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushinteger(state, -1);
            return 1;
        }
        ecs::AnimationTransition transition = {};
        transition.from_state = from_state;
        transition.to_state = to_state;
        transition.blend_duration = blend_duration;
        state_machine->transitions.push_back(transition);
        lua_pushinteger(state, static_cast<lua_Integer>(state_machine->transitions.size() - 1));
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMAddCondition(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const uint32 transition_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        const char* parameter_name = luaL_checkstring(state, 2);
        const char* op_name = luaL_checkstring(state, 3);
        const float threshold = lua_gettop(state) >= 4 ? static_cast<float>(luaL_checknumber(state, 4)) : 0.0f;
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine || transition_index >= state_machine->transitions.size())
        {
            lua_pushboolean(state, false);
            return 1;
        }
        ecs::TransitionCondition condition = {};
        condition.parameter = parameter_name;
        condition.threshold = threshold;
        if (std::strcmp(op_name, "greater") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::Greater;
        }
        else if (std::strcmp(op_name, "less") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::Less;
        }
        else if (std::strcmp(op_name, "equal") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::Equal;
        }
        else if (std::strcmp(op_name, "notequal") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::NotEqual;
        }
        else if (std::strcmp(op_name, "true") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::IsTrue;
        }
        else if (std::strcmp(op_name, "false") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::IsFalse;
        }
        else if (std::strcmp(op_name, "trigger") == 0)
        {
            condition.op = ecs::TransitionCondition::Op::TriggerSet;
        }
        else
        {
            condition.op = ecs::TransitionCondition::Op::Greater;
        }
        state_machine->transitions[transition_index].conditions.push_back(condition);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMSetExitTime(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const uint32 transition_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        const float exit_time = static_cast<float>(luaL_checknumber(state, 2));
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine || transition_index >= state_machine->transitions.size())
        {
            lua_pushboolean(state, false);
            return 1;
        }
        state_machine->transitions[transition_index].has_exit_time = true;
        state_machine->transitions[transition_index].exit_time = exit_time;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSMSetDefaultState(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const int32 state_index = static_cast<int32>(luaL_checkinteger(state, 1));
        ecs::AnimationStateMachineComponent* state_machine = GetSelfStateMachine(runtime);
        if (!state_machine)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        state_machine->default_state = state_index;
        lua_pushboolean(state, true);
        return 1;
    }

    ecs::SequenceComponent* LuaScriptRuntime::GetSequence(LuaScriptRuntime* runtime, lua_State* state, int entity_arg)
    {
        if (!runtime || !runtime->current_context.scene)
        {
            return nullptr;
        }
        const ecs::Entity entity = lua_gettop(state) >= entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, entity_arg)) : runtime->current_context.entity;
        return runtime->current_context.scene->GetComponent<ecs::SequenceComponent>(entity);
    }

    int LuaScriptRuntime::LuaSequenceHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        lua_pushboolean(state, GetSequence(runtime, state, 1) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->AddComponent<ecs::SequenceComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceAddTrack(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        luaL_checktype(state, 2, LUA_TTABLE);

        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushinteger(state, -1);
            return 1;
        }

        ecs::SequenceTrack track = {};

        lua_getfield(state, 2, "target");
        track.target = lua_isnumber(state, -1) ? static_cast<ecs::Entity>(lua_tointeger(state, -1)) : ecs::INVALID_ENTITY;
        lua_pop(state, 1);

        lua_getfield(state, 2, "type");
        const char* type_name = lua_tostring(state, -1);
        bool valid_type = true;
        if (!type_name || std::strcmp(type_name, "position") == 0)
        {
            track.type = ecs::SequenceTrackType::Position;
        }
        else if (std::strcmp(type_name, "rotation") == 0)
        {
            track.type = ecs::SequenceTrackType::Rotation;
        }
        else if (std::strcmp(type_name, "camera_fov") == 0)
        {
            track.type = ecs::SequenceTrackType::CameraFov;
        }
        else if (std::strcmp(type_name, "camera_switch") == 0)
        {
            track.type = ecs::SequenceTrackType::CameraSwitch;
        }
        else if (std::strcmp(type_name, "event") == 0)
        {
            track.type = ecs::SequenceTrackType::Event;
        }
        else
        {
            valid_type = false;
            backlog::Post(String("[Sequence] unknown track type: ") + type_name, backlog::LogLevel::Warning);
        }
        lua_pop(state, 1);
        if (!valid_type)
        {
            lua_pushinteger(state, -1);
            return 1;
        }

        lua_getfield(state, 2, "keys");
        const int32 key_count = lua_istable(state, -1) ? static_cast<int32>(lua_rawlen(state, -1)) : 0;
        track.keys.reserve(static_cast<Size>(key_count));
        for (int32 key_index = 0; key_index < key_count; ++key_index)
        {
            lua_rawgeti(state, -1, key_index + 1);
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                continue;
            }

            ecs::SequenceKey key = {};
            lua_getfield(state, -1, "time");
            key.time = static_cast<float>(lua_tonumber(state, -1));
            lua_pop(state, 1);

            switch (track.type)
            {
            case ecs::SequenceTrackType::Position:
            {
                lua_getfield(state, -1, "x");
                key.value.x = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                lua_getfield(state, -1, "y");
                key.value.y = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                lua_getfield(state, -1, "z");
                key.value.z = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                break;
            }
            case ecs::SequenceTrackType::Rotation:
            {
                lua_getfield(state, -1, "pitch");
                const float pitch = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                lua_getfield(state, -1, "yaw");
                const float yaw = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                lua_getfield(state, -1, "roll");
                const float roll = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                XMStoreFloat4(&key.value, XMQuaternionNormalize(XMQuaternionRotationRollPitchYaw(pitch, yaw, roll)));
                break;
            }
            case ecs::SequenceTrackType::CameraFov:
            {
                lua_getfield(state, -1, "value");
                key.value.x = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
                break;
            }
            case ecs::SequenceTrackType::CameraSwitch:
            {
                lua_getfield(state, -1, "camera");
                key.camera = lua_isnumber(state, -1) ? static_cast<ecs::Entity>(lua_tointeger(state, -1)) : ecs::INVALID_ENTITY;
                lua_pop(state, 1);
                break;
            }
            case ecs::SequenceTrackType::Event:
            {
                lua_getfield(state, -1, "name");
                if (const char* event_name = lua_tostring(state, -1))
                {
                    key.event_name = event_name;
                }
                lua_pop(state, 1);
                break;
            }
            default:
                break;
            }

            track.keys.push_back(std::move(key));
            lua_pop(state, 1);
        }
        lua_pop(state, 1);

        std::sort(track.keys.begin(), track.keys.end(), [](const ecs::SequenceKey& lhs, const ecs::SequenceKey& rhs) { return lhs.time < rhs.time; });
        sequence->tracks.push_back(std::move(track));
        lua_pushinteger(state, static_cast<lua_Integer>(sequence->tracks.size() - 1));
        return 1;
    }

    int LuaScriptRuntime::LuaSequencePlay(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        sequence->time = 0.0f;
        sequence->event_scan_time = -1.0f;
        sequence->started = true;
        sequence->SetPlaying(true);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceStop(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        sequence->SetPlaying(false);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceSetTime(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const float time = static_cast<float>(luaL_checknumber(state, 2));
        sequence->time = time;
        sequence->event_scan_time = time;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceSetLoop(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        sequence->SetLoop(lua_toboolean(state, 2) != 0);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceSetDuration(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        if (!sequence)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        sequence->duration = static_cast<float>(luaL_checknumber(state, 2));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaSequenceIsPlaying(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const ecs::SequenceComponent* sequence = GetSequence(runtime, state, 1);
        lua_pushboolean(state, sequence && sequence->IsPlaying());
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::AnimationComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->AddComponent<ecs::AnimationComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationPlay(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const uint32 clip_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        const float blend = lua_gettop(state) >= 2 ? static_cast<float>(luaL_checknumber(state, 2)) : 0.0f;
        StartAnimationClip(*animation, clip_index, blend);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationPlayByName(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const char* name = luaL_checkstring(state, 1);
        const float blend = lua_gettop(state) >= 2 ? static_cast<float>(luaL_checknumber(state, 2)) : 0.0f;
        for (Size i = 0; i < animation->clips.size(); ++i)
        {
            if (animation->clips[i] && animation->clips[i]->name == name)
            {
                StartAnimationClip(*animation, static_cast<uint32>(i), blend);
                lua_pushboolean(state, true);
                return 1;
            }
        }
        lua_pushboolean(state, false);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationCrossfade(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const uint32 clip_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        const float duration = static_cast<float>(luaL_checknumber(state, 2));
        StartAnimationClip(*animation, clip_index, duration);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSetSpeed(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        animation->speed = static_cast<float>(luaL_checknumber(state, 1));
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationSetLoop(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        animation->loop = lua_toboolean(state, 1) != 0;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationPause(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        animation->playing = false;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationResume(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        animation->playing = true;
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationAddClipEvent(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        const uint32 clip_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        const float time_seconds = static_cast<float>(luaL_checknumber(state, 2));
        const char* name = luaL_checkstring(state, 3);
        if (!animation || clip_index >= animation->clips.size() || !animation->clips[clip_index])
        {
            lua_pushboolean(state, false);
            return 1;
        }
        resource::AnimationClip& clip = *animation->clips[clip_index];
        for (const resource::AnimationEventMarker& existing : clip.events)
        {
            if (existing.time_seconds == time_seconds && existing.name == name)
            {
                lua_pushboolean(state, true); // shared clip: skip duplicate
                return 1;
            }
        }
        clip.events.push_back({ time_seconds, name });
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationIsPlaying(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        lua_pushboolean(state, animation && animation->playing);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetClipCount(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        lua_pushinteger(state, animation ? static_cast<lua_Integer>(animation->clips.size()) : 0);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetCurrentClip(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        lua_pushinteger(state, animation ? static_cast<lua_Integer>(animation->current_clip_index) : -1);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetClipName(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        const uint32 clip_index = static_cast<uint32>(luaL_checkinteger(state, 1));
        if (!animation || clip_index >= animation->clips.size() || !animation->clips[clip_index])
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushstring(state, animation->clips[clip_index]->name.c_str());
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetClipDuration(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        const char* name = luaL_checkstring(state, 1);
        if (animation)
        {
            for (Size i = 0; i < animation->clips.size(); ++i)
            {
                const std::shared_ptr<resource::AnimationClip>& clip = animation->clips[i];
                if (clip && clip->name == name)
                {
                    lua_pushnumber(state, clip->DurationSeconds());
                    return 1;
                }
            }
        }
        lua_pushnil(state);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationGetNormalizedTime(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation || animation->current_clip_index >= animation->clips.size() || !animation->clips[animation->current_clip_index])
        {
            lua_pushnil(state);
            return 1;
        }
        const std::shared_ptr<resource::AnimationClip>& clip = animation->clips[animation->current_clip_index];
        const float duration_seconds = clip->DurationSeconds();
        float normalized = duration_seconds > 0.0f ? animation->time / duration_seconds : 0.0f;
        if (normalized < 0.0f)
        {
            normalized = 0.0f;
        }
        else if (normalized > 1.0f)
        {
            normalized = 1.0f;
        }
        lua_pushnumber(state, normalized);
        return 1;
    }

    int LuaScriptRuntime::LuaAnimationIsCurrentFinished(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        ecs::AnimationComponent* animation = GetSelfAnimation(runtime);
        if (!animation || animation->loop || animation->current_clip_index >= animation->clips.size() || !animation->clips[animation->current_clip_index])
        {
            lua_pushboolean(state, false);
            return 1;
        }
        const std::shared_ptr<resource::AnimationClip>& clip = animation->clips[animation->current_clip_index];
        const float duration_seconds = clip->DurationSeconds();
        lua_pushboolean(state, duration_seconds > 0.0f && animation->time >= duration_seconds - 1e-4f);
        return 1;
    }

    int LuaScriptRuntime::LuaColliderHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::Collider3DComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaColliderAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::Collider3DComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaColliderIsEnabled(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::Collider3DComponent* collider = runtime->current_context.scene->GetComponent<ecs::Collider3DComponent>(entity);
        if (!collider)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        lua_pushboolean(state, collider->IsEnabled());
        return 1;
    }

    int LuaScriptRuntime::LuaColliderSetEnabled(lua_State* state)
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
        const bool value = lua_toboolean(state, has_entity_arg ? 2 : 1) != 0;
        ecs::Collider3DComponent* collider = runtime->current_context.scene->GetComponent<ecs::Collider3DComponent>(entity);
        if (!collider)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        collider->SetEnabled(value);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaColliderIsTrigger(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::Collider3DComponent* collider = runtime->current_context.scene->GetComponent<ecs::Collider3DComponent>(entity);
        if (!collider)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        lua_pushboolean(state, collider->IsTrigger());
        return 1;
    }

    int LuaScriptRuntime::LuaColliderSetTrigger(lua_State* state)
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
        const bool value = lua_toboolean(state, has_entity_arg ? 2 : 1) != 0;
        ecs::Collider3DComponent* collider = runtime->current_context.scene->GetComponent<ecs::Collider3DComponent>(entity);
        if (!collider)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        collider->SetTrigger(value);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaRigidbodyHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::Rigidbody3DComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaRigidbodyAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::Rigidbody3DComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaRigidbodyGetVelocity(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::Rigidbody3DComponent* rb = runtime->current_context.scene->GetComponent<ecs::Rigidbody3DComponent>(entity);
        if (!rb)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, rb->linear_velocity.x);
        lua_pushnumber(state, rb->linear_velocity.y);
        lua_pushnumber(state, rb->linear_velocity.z);
        return 3;
    }

    int LuaScriptRuntime::LuaRigidbodySetVelocity(lua_State* state)
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
        ecs::Rigidbody3DComponent* rb = runtime->current_context.scene->GetComponent<ecs::Rigidbody3DComponent>(entity);
        if (!rb)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        rb->linear_velocity.x = static_cast<float>(luaL_checknumber(state, value_index));
        rb->linear_velocity.y = static_cast<float>(luaL_checknumber(state, value_index + 1));
        rb->linear_velocity.z = static_cast<float>(luaL_checknumber(state, value_index + 2));
        rb->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaRigidbodyGetAngularVelocity(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::Rigidbody3DComponent* rb = runtime->current_context.scene->GetComponent<ecs::Rigidbody3DComponent>(entity);
        if (!rb)
        {
            lua_pushnil(state);
            return 1;
        }

        lua_pushnumber(state, rb->angular_velocity.x);
        lua_pushnumber(state, rb->angular_velocity.y);
        lua_pushnumber(state, rb->angular_velocity.z);
        return 3;
    }

    int LuaScriptRuntime::LuaRigidbodySetAngularVelocity(lua_State* state)
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
        ecs::Rigidbody3DComponent* rb = runtime->current_context.scene->GetComponent<ecs::Rigidbody3DComponent>(entity);
        if (!rb)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        rb->angular_velocity.x = static_cast<float>(luaL_checknumber(state, value_index));
        rb->angular_velocity.y = static_cast<float>(luaL_checknumber(state, value_index + 1));
        rb->angular_velocity.z = static_cast<float>(luaL_checknumber(state, value_index + 2));
        rb->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsAddForce(lua_State* state)
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
        physics::PhysicsWorld* physics_world = runtime->current_context.scene->GetPhysicsWorld();
        if (!physics_world)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const float3 force = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        physics_world->AddForce(entity, force);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsAddImpulse(lua_State* state)
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
        physics::PhysicsWorld* physics_world = runtime->current_context.scene->GetPhysicsWorld();
        if (!physics_world)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const float3 impulse = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        physics_world->AddImpulse(entity, impulse);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsAddTorque(lua_State* state)
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
        physics::PhysicsWorld* physics_world = runtime->current_context.scene->GetPhysicsWorld();
        if (!physics_world)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const float3 torque = {
            static_cast<float>(luaL_checknumber(state, value_index)),
            static_cast<float>(luaL_checknumber(state, value_index + 1)),
            static_cast<float>(luaL_checknumber(state, value_index + 2))
        };
        physics_world->AddTorque(entity, torque);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsRaycast(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        math::Ray ray;
        ray.origin = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        ray.direction = {
            static_cast<float>(luaL_checknumber(state, 4)),
            static_cast<float>(luaL_checknumber(state, 5)),
            static_cast<float>(luaL_checknumber(state, 6))
        };
        const float max_distance = lua_gettop(state) >= 7 ? static_cast<float>(luaL_checknumber(state, 7)) : 1000.0f;

        ecs::RayCastHit hit;
        if (!runtime->current_context.scene->RayCastCollider3D(ray, hit, max_distance))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_newtable(state);
        lua_pushinteger(state, static_cast<lua_Integer>(hit.entity));
        lua_setfield(state, -2, "entity");
        lua_pushnumber(state, hit.distance);
        lua_setfield(state, -2, "distance");
        lua_newtable(state);
        lua_pushnumber(state, hit.point.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, hit.point.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, hit.point.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "point");
        lua_newtable(state);
        lua_pushnumber(state, hit.normal.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, hit.normal.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, hit.normal.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "normal");
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsSphereCast(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene || !runtime->current_context.scene->GetPhysicsWorld())
        {
            lua_pushnil(state);
            return 1;
        }

        float3 origin = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        float3 direction = {
            static_cast<float>(luaL_checknumber(state, 4)),
            static_cast<float>(luaL_checknumber(state, 5)),
            static_cast<float>(luaL_checknumber(state, 6))
        };
        const float radius = static_cast<float>(luaL_checknumber(state, 7));
        const float max_distance = lua_gettop(state) >= 8 ? static_cast<float>(luaL_checknumber(state, 8)) : 1000.0f;

        physics::RayCastHit hit;
        if (!runtime->current_context.scene->GetPhysicsWorld()->SphereCast(origin, direction, radius, max_distance, hit))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_newtable(state);
        lua_pushinteger(state, static_cast<lua_Integer>(hit.entity));
        lua_setfield(state, -2, "entity");
        lua_pushnumber(state, hit.distance);
        lua_setfield(state, -2, "distance");
        lua_newtable(state);
        lua_pushnumber(state, hit.point.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, hit.point.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, hit.point.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "point");
        lua_newtable(state);
        lua_pushnumber(state, hit.normal.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, hit.normal.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, hit.normal.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "normal");
        return 1;
    }

    int LuaScriptRuntime::LuaPhysicsOverlapSphere(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushnil(state);
            return 1;
        }

        math::Sphere sphere;
        sphere.center = {
            static_cast<float>(luaL_checknumber(state, 1)),
            static_cast<float>(luaL_checknumber(state, 2)),
            static_cast<float>(luaL_checknumber(state, 3))
        };
        sphere.radius = static_cast<float>(luaL_checknumber(state, 4));

        Vector<ecs::OverlapHit> hits;
        runtime->current_context.scene->OverlapCollider3D(sphere, hits);

        lua_newtable(state);
        for (Size i = 0; i < hits.size(); ++i)
        {
            lua_pushinteger(state, static_cast<lua_Integer>(hits[i].entity));
            lua_rawseti(state, -2, static_cast<lua_Integer>(i + 1));
        }
        return 1;
    }

    int LuaScriptRuntime::LuaInputMousePosition(lua_State* state)
    {
        const io::MouseState& mouse = io::GetMouseState();
        lua_pushnumber(state, mouse.position.x);
        lua_pushnumber(state, mouse.position.y);
        return 2;
    }

    int LuaScriptRuntime::LuaInputMouseDelta(lua_State* state)
    {
        const io::MouseState& mouse = io::GetMouseState();
        lua_pushnumber(state, mouse.delta_position.x);
        lua_pushnumber(state, mouse.delta_position.y);
        return 2;
    }

    int LuaScriptRuntime::LuaInputMouseWheel(lua_State* state)
    {
        lua_pushnumber(state, io::GetMouseState().delta_wheel);
        return 1;
    }

    int LuaScriptRuntime::LuaInputMouseButton(lua_State* state)
    {
        const int button = static_cast<int>(luaL_checkinteger(state, 1));
        const io::MouseState& mouse = io::GetMouseState();
        bool pressed = false;
        if (button == 0)
        {
            pressed = mouse.left_button_press;
        }
        else if (button == 1)
        {
            pressed = mouse.right_button_press;
        }
        else if (button == 2)
        {
            pressed = mouse.middle_button_press;
        }
        lua_pushboolean(state, pressed);
        return 1;
    }

    int LuaScriptRuntime::LuaCameraScreenToRay(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->view_resolver)
        {
            lua_pushnil(state);
            return 1;
        }

        const float2 point = { static_cast<float>(luaL_checknumber(state, 1)), static_cast<float>(luaL_checknumber(state, 2)) };
        rendering::View* view = runtime->view_resolver(point);
        if (!view)
        {
            lua_pushnil(state);
            return 1;
        }

        math::Ray ray = {};
        if (!view->ScreenToRay(point, ray))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_newtable(state);
        lua_newtable(state);
        lua_pushnumber(state, ray.origin.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, ray.origin.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, ray.origin.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "origin");
        lua_newtable(state);
        lua_pushnumber(state, ray.direction.x); lua_setfield(state, -2, "x");
        lua_pushnumber(state, ray.direction.y); lua_setfield(state, -2, "y");
        lua_pushnumber(state, ray.direction.z); lua_setfield(state, -2, "z");
        lua_setfield(state, -2, "direction");
        return 1;
    }

    int LuaScriptRuntime::LuaCameraPick(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->view_resolver)
        {
            lua_pushnil(state);
            return 1;
        }

        const float2 point = { static_cast<float>(luaL_checknumber(state, 1)), static_cast<float>(luaL_checknumber(state, 2)) };
        rendering::View* view = runtime->view_resolver(point);
        if (!view)
        {
            lua_pushnil(state);
            return 1;
        }

        ecs::RayCastHit hit;
        if (!view->RayCast(point, hit))
        {
            lua_pushnil(state);
            return 1;
        }

        lua_newtable(state);
        lua_pushinteger(state, static_cast<lua_Integer>(hit.entity));
        lua_setfield(state, -2, "entity");
        lua_pushnumber(state, hit.distance);
        lua_setfield(state, -2, "distance");
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourceHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::AudioSourceComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourceAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const int arg_count = lua_gettop(state);
        const bool has_entity_arg = arg_count >= 1 && lua_isinteger(state, 1);
        const ecs::Entity entity = has_entity_arg ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::AudioSourceComponent* source = runtime->current_context.scene->AddComponent<ecs::AudioSourceComponent>(entity);
        if (source && has_entity_arg && arg_count >= 2 && lua_isstring(state, 2))
        {
            source->sound_asset_path = lua_tostring(state, 2);
            source->SetDirty();
        }
        else if (source && !has_entity_arg && arg_count >= 1 && lua_isstring(state, 1))
        {
            source->sound_asset_path = lua_tostring(state, 1);
            source->SetDirty();
        }
        lua_pushboolean(state, source != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourcePlay(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::AudioSourceComponent* source = runtime->current_context.scene->GetComponent<ecs::AudioSourceComponent>(entity);
        if (!source)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        source->SetPlaying(true);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourceStop(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::AudioSourceComponent* source = runtime->current_context.scene->GetComponent<ecs::AudioSourceComponent>(entity);
        if (!source)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        source->SetPlaying(false);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourceIsPlaying(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::AudioSourceComponent* source = runtime->current_context.scene->GetComponent<ecs::AudioSourceComponent>(entity);
        if (!source)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        lua_pushboolean(state, source->IsPlaying());
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSourceSetVolume(lua_State* state)
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
        const float volume = static_cast<float>(luaL_checknumber(state, has_entity_arg ? 2 : 1));
        ecs::AudioSourceComponent* source = runtime->current_context.scene->GetComponent<ecs::AudioSourceComponent>(entity);
        if (!source)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        source->volume = volume;
        source->SetDirty();
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioPlayOneShot(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->audio_mixer)
        {
            lua_pushinteger(state, static_cast<lua_Integer>(audio::invalid_voice_handle));
            return 1;
        }

        const char* path = luaL_checkstring(state, 1);
        const float volume = static_cast<float>(luaL_optnumber(state, 2, 1.0));

        const String resolved_path = project::ResolveProjectContentPath(runtime->content_root, path);
        auto sound = resource::LoadSoundFile(resolved_path);
        if (!sound || !sound->IsValid())
        {
            won::backlog::Post(String("[Audio] play_oneshot: sound not found: ") + resolved_path, won::backlog::LogLevel::Warning);
            lua_pushinteger(state, static_cast<lua_Integer>(audio::invalid_voice_handle));
            return 1;
        }

        audio::VoiceParams params;
        params.volume = volume;
        params.loop = false;
        const audio::VoiceHandle handle = runtime->audio_mixer->Play(*sound, params);
        lua_pushinteger(state, static_cast<lua_Integer>(handle));
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSetMasterVolume(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const float volume = static_cast<float>(luaL_checknumber(state, 1));
        if (!runtime || !runtime->audio_mixer)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        runtime->audio_mixer->SetMasterVolume(volume);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioGetMasterVolume(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->audio_mixer)
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, runtime->audio_mixer->GetMasterVolume());
        return 1;
    }

    int LuaScriptRuntime::LuaAudioSetSubmixVolume(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        const float volume = static_cast<float>(luaL_checknumber(state, 2));
        if (!runtime || !runtime->audio_mixer)
        {
            lua_pushboolean(state, false);
            return 1;
        }
        runtime->audio_mixer->SetSubmixVolume(name, volume);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioGetSubmixVolume(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        const char* name = luaL_checkstring(state, 1);
        if (!runtime || !runtime->audio_mixer)
        {
            lua_pushnil(state);
            return 1;
        }
        lua_pushnumber(state, runtime->audio_mixer->GetSubmixVolume(name));
        return 1;
    }

    int LuaScriptRuntime::LuaAudioListenerHas(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        lua_pushboolean(state, runtime->current_context.scene->GetComponent<ecs::AudioListenerComponent>(entity) != nullptr);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioListenerAdd(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        runtime->current_context.scene->AddComponent<ecs::AudioListenerComponent>(entity);
        lua_pushboolean(state, true);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioListenerIsEnabled(lua_State* state)
    {
        LuaScriptRuntime* runtime = static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
        if (!runtime || !runtime->current_context.scene)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        const ecs::Entity entity = lua_gettop(state) >= 1 ? static_cast<ecs::Entity>(luaL_checkinteger(state, 1)) : runtime->current_context.entity;
        ecs::AudioListenerComponent* listener = runtime->current_context.scene->GetComponent<ecs::AudioListenerComponent>(entity);
        if (!listener)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        lua_pushboolean(state, listener->enabled);
        return 1;
    }

    int LuaScriptRuntime::LuaAudioListenerSetEnabled(lua_State* state)
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
        const bool value = lua_toboolean(state, has_entity_arg ? 2 : 1) != 0;
        ecs::AudioListenerComponent* listener = runtime->current_context.scene->GetComponent<ecs::AudioListenerComponent>(entity);
        if (!listener)
        {
            lua_pushboolean(state, false);
            return 1;
        }

        listener->enabled = value;
        lua_pushboolean(state, true);
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
        lua_pushcclosure(lua_state, LuaEntityCreate, 1);
        lua_setfield(lua_state, -2, "create");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntitySpawnPrefab, 1);
        lua_setfield(lua_state, -2, "spawn_prefab");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntitySpawnPrefabChild, 1);
        lua_setfield(lua_state, -2, "spawn_prefab_child");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEntityPreloadPrefab, 1);
        lua_setfield(lua_state, -2, "preload_prefab");
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
        lua_pushcclosure(lua_state, LuaTransformAdd, 1);
        lua_setfield(lua_state, -2, "add");
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
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformGetForward, 1);
        lua_setfield(lua_state, -2, "get_forward");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformGetRotation, 1);
        lua_setfield(lua_state, -2, "get_rotation");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformSetRotation, 1);
        lua_setfield(lua_state, -2, "set_rotation");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaTransformSetEuler, 1);
        lua_setfield(lua_state, -2, "set_euler");
        lua_setfield(lua_state, -2, "transform");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialGetBaseColor, 1);
        lua_setfield(lua_state, -2, "get_base_color");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialSetBaseColor, 1);
        lua_setfield(lua_state, -2, "set_base_color");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialGetRoughness, 1);
        lua_setfield(lua_state, -2, "get_roughness");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialSetRoughness, 1);
        lua_setfield(lua_state, -2, "set_roughness");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialGetMetallic, 1);
        lua_setfield(lua_state, -2, "get_metallic");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialSetMetallic, 1);
        lua_setfield(lua_state, -2, "set_metallic");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaMaterialFork, 1);
        lua_setfield(lua_state, -2, "fork");
        lua_setfield(lua_state, -2, "material");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetActive, 1);
        lua_setfield(lua_state, -2, "set_active");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetSkyColors, 1);
        lua_setfield(lua_state, -2, "set_sky_colors");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetAmbient, 1);
        lua_setfield(lua_state, -2, "set_ambient");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetSunIntensity, 1);
        lua_setfield(lua_state, -2, "set_sun_intensity");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetSunDirection, 1);
        lua_setfield(lua_state, -2, "set_sun_direction");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetSkyType, 1);
        lua_setfield(lua_state, -2, "set_sky_type");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetDiffuseGIMode, 1);
        lua_setfield(lua_state, -2, "set_diffuse_gi_mode");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetReflectionMode, 1);
        lua_setfield(lua_state, -2, "set_reflection_mode");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetDirectSunActive, 1);
        lua_setfield(lua_state, -2, "set_direct_sun_active");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetAtmosphere, 1);
        lua_setfield(lua_state, -2, "set_atmosphere");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetScatteringCoefficients, 1);
        lua_setfield(lua_state, -2, "set_scattering_coefficients");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetCloud, 1);
        lua_setfield(lua_state, -2, "set_cloud");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetCloudColor, 1);
        lua_setfield(lua_state, -2, "set_cloud_color");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEnvironmentSetCloudMotion, 1);
        lua_setfield(lua_state, -2, "set_cloud_motion");
        lua_setfield(lua_state, -2, "environment");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaParticleEmitter3DHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaParticleEmitter3DAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaParticleEmitter3DSetActive, 1);
        lua_setfield(lua_state, -2, "set_active");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaParticleEmitter3DSetSpawnRate, 1);
        lua_setfield(lua_state, -2, "set_spawn_rate");
        lua_setfield(lua_state, -2, "particle_emitter_3d");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationPlay, 1);
        lua_setfield(lua_state, -2, "play");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationPlayByName, 1);
        lua_setfield(lua_state, -2, "play_by_name");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationCrossfade, 1);
        lua_setfield(lua_state, -2, "crossfade");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSetSpeed, 1);
        lua_setfield(lua_state, -2, "set_speed");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSetLoop, 1);
        lua_setfield(lua_state, -2, "set_loop");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationPause, 1);
        lua_setfield(lua_state, -2, "pause");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationResume, 1);
        lua_setfield(lua_state, -2, "resume");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationIsPlaying, 1);
        lua_setfield(lua_state, -2, "is_playing");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetClipCount, 1);
        lua_setfield(lua_state, -2, "get_clip_count");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetCurrentClip, 1);
        lua_setfield(lua_state, -2, "get_current_clip");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetClipName, 1);
        lua_setfield(lua_state, -2, "get_clip_name");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetClipDuration, 1);
        lua_setfield(lua_state, -2, "get_clip_duration");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetNormalizedTime, 1);
        lua_setfield(lua_state, -2, "get_normalized_time");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationIsCurrentFinished, 1);
        lua_setfield(lua_state, -2, "is_current_finished");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationAddClipEvent, 1);
        lua_setfield(lua_state, -2, "add_clip_event");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSetBool, 1);
        lua_setfield(lua_state, -2, "set_bool");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSetFloat, 1);
        lua_setfield(lua_state, -2, "set_float");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSetTrigger, 1);
        lua_setfield(lua_state, -2, "set_trigger");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationGetState, 1);
        lua_setfield(lua_state, -2, "get_state");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMAdd, 1);
        lua_setfield(lua_state, -2, "add_state_machine");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMHas, 1);
        lua_setfield(lua_state, -2, "has_state_machine");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMAddParameter, 1);
        lua_setfield(lua_state, -2, "sm_add_parameter");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMAddState, 1);
        lua_setfield(lua_state, -2, "sm_add_state");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMAddTransition, 1);
        lua_setfield(lua_state, -2, "sm_add_transition");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMAddCondition, 1);
        lua_setfield(lua_state, -2, "sm_add_condition");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMSetExitTime, 1);
        lua_setfield(lua_state, -2, "sm_set_exit_time");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAnimationSMSetDefaultState, 1);
        lua_setfield(lua_state, -2, "sm_set_default_state");
        lua_setfield(lua_state, -2, "animation");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceAddTrack, 1);
        lua_setfield(lua_state, -2, "add_track");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequencePlay, 1);
        lua_setfield(lua_state, -2, "play");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceStop, 1);
        lua_setfield(lua_state, -2, "stop");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceSetTime, 1);
        lua_setfield(lua_state, -2, "set_time");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceSetLoop, 1);
        lua_setfield(lua_state, -2, "set_loop");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceSetDuration, 1);
        lua_setfield(lua_state, -2, "set_duration");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSequenceIsPlaying, 1);
        lua_setfield(lua_state, -2, "is_playing");
        lua_setfield(lua_state, -2, "sequence");

        lua_newtable(lua_state);
        lua_pushcfunction(lua_state, LuaInputIsKeyDown);
        lua_setfield(lua_state, -2, "is_key_down");
        lua_pushcfunction(lua_state, LuaInputIsKeyPressed);
        lua_setfield(lua_state, -2, "is_key_pressed");
        lua_pushcfunction(lua_state, LuaInputIsActionDown);
        lua_setfield(lua_state, -2, "is_action_down");
        lua_pushcfunction(lua_state, LuaInputIsActionPressed);
        lua_setfield(lua_state, -2, "is_action_pressed");
        lua_pushcfunction(lua_state, LuaInputGetActionValue);
        lua_setfield(lua_state, -2, "get_action_value");
        lua_pushcfunction(lua_state, LuaInputGetActionAxis2D);
        lua_setfield(lua_state, -2, "get_action_axis2d");
        lua_pushcfunction(lua_state, LuaInputGetGamepadAxis);
        lua_setfield(lua_state, -2, "get_gamepad_axis");
        lua_pushcfunction(lua_state, LuaInputIsGamepadConnected);
        lua_setfield(lua_state, -2, "is_gamepad_connected");
        lua_pushcfunction(lua_state, LuaInputMousePosition);
        lua_setfield(lua_state, -2, "mouse_position");
        lua_pushcfunction(lua_state, LuaInputMouseDelta);
        lua_setfield(lua_state, -2, "mouse_delta");
        lua_pushcfunction(lua_state, LuaInputMouseWheel);
        lua_setfield(lua_state, -2, "mouse_wheel");
        lua_pushcfunction(lua_state, LuaInputMouseButton);
        lua_setfield(lua_state, -2, "mouse_button");
        lua_setfield(lua_state, -2, "input");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaCameraScreenToRay, 1);
        lua_setfield(lua_state, -2, "screen_to_ray");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaCameraPick, 1);
        lua_setfield(lua_state, -2, "pick");
        lua_setfield(lua_state, -2, "camera");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneFindByName, 1);
        lua_setfield(lua_state, -2, "find_by_name");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneLoad, 1);
        lua_setfield(lua_state, -2, "load");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneLoadAdditive, 1);
        lua_setfield(lua_state, -2, "load_additive");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneUnloadAdditive, 1);
        lua_setfield(lua_state, -2, "unload_additive");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneIsLoading, 1);
        lua_setfield(lua_state, -2, "is_loading");
        lua_setfield(lua_state, -2, "scene");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaText2DSetString, 1);
        lua_setfield(lua_state, -2, "set_string");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaText2DGetString, 1);
        lua_setfield(lua_state, -2, "get_string");
        lua_setfield(lua_state, -2, "text2d");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaText3DSetString, 1);
        lua_setfield(lua_state, -2, "set_string");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaText3DGetString, 1);
        lua_setfield(lua_state, -2, "get_string");
        lua_setfield(lua_state, -2, "text3d");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderIsEnabled, 1);
        lua_setfield(lua_state, -2, "is_enabled");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderSetEnabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderIsTrigger, 1);
        lua_setfield(lua_state, -2, "is_trigger");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaColliderSetTrigger, 1);
        lua_setfield(lua_state, -2, "set_trigger");
        lua_setfield(lua_state, -2, "collider");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodyHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodyAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodyGetVelocity, 1);
        lua_setfield(lua_state, -2, "get_velocity");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodySetVelocity, 1);
        lua_setfield(lua_state, -2, "set_velocity");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodyGetAngularVelocity, 1);
        lua_setfield(lua_state, -2, "get_angular_velocity");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaRigidbodySetAngularVelocity, 1);
        lua_setfield(lua_state, -2, "set_angular_velocity");
        lua_setfield(lua_state, -2, "rigidbody");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsAddForce, 1);
        lua_setfield(lua_state, -2, "add_force");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsAddImpulse, 1);
        lua_setfield(lua_state, -2, "add_impulse");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsAddTorque, 1);
        lua_setfield(lua_state, -2, "add_torque");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsRaycast, 1);
        lua_setfield(lua_state, -2, "raycast");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsSphereCast, 1);
        lua_setfield(lua_state, -2, "spherecast");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaPhysicsOverlapSphere, 1);
        lua_setfield(lua_state, -2, "overlap_sphere");
        lua_setfield(lua_state, -2, "physics");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaNavFindPath, 1);
        lua_setfield(lua_state, -2, "find_path");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaNavNearestPoint, 1);
        lua_setfield(lua_state, -2, "nearest_point");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaNavIsReady, 1);
        lua_setfield(lua_state, -2, "is_ready");
        lua_setfield(lua_state, -2, "nav");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaWaterAddRipple, 1);
        lua_setfield(lua_state, -2, "add_ripple");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaWaterSampleHeight, 1);
        lua_setfield(lua_state, -2, "sample_height");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaWaterSampleSurface, 1);
        lua_setfield(lua_state, -2, "sample_surface");
        lua_setfield(lua_state, -2, "water");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAISetTree, 1);
        lua_setfield(lua_state, -2, "set_tree");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAISet, 1);
        lua_setfield(lua_state, -2, "set");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIGet, 1);
        lua_setfield(lua_state, -2, "get");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIAddBehaviorTree, 1);
        lua_setfield(lua_state, -2, "add_behavior_tree");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIAddNavAgent, 1);
        lua_setfield(lua_state, -2, "add_nav_agent");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIHasBehaviorTree, 1);
        lua_setfield(lua_state, -2, "has_behavior_tree");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIHasNavAgent, 1);
        lua_setfield(lua_state, -2, "has_nav_agent");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ai::BehaviorTree::Status::Success));
        lua_setfield(lua_state, -2, "success");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ai::BehaviorTree::Status::Failure));
        lua_setfield(lua_state, -2, "failure");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ai::BehaviorTree::Status::Running));
        lua_setfield(lua_state, -2, "running");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIMoveTo, 1);
        lua_setfield(lua_state, -2, "move_to");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIStop, 1);
        lua_setfield(lua_state, -2, "stop");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAIGetMoveState, 1);
        lua_setfield(lua_state, -2, "get_move_state");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAISetMoveSpeed, 1);
        lua_setfield(lua_state, -2, "set_move_speed");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ecs::NavAgentComponent::MoveState::Idle));
        lua_setfield(lua_state, -2, "move_idle");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ecs::NavAgentComponent::MoveState::Moving));
        lua_setfield(lua_state, -2, "move_moving");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ecs::NavAgentComponent::MoveState::Arrived));
        lua_setfield(lua_state, -2, "move_arrived");
        lua_pushinteger(lua_state, static_cast<lua_Integer>(ecs::NavAgentComponent::MoveState::Failed));
        lua_setfield(lua_state, -2, "move_failed");
        lua_setfield(lua_state, -2, "ai");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSettingsGet, 1);
        lua_setfield(lua_state, -2, "get");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSettingsSet, 1);
        lua_setfield(lua_state, -2, "set");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSettingsSave, 1);
        lua_setfield(lua_state, -2, "save");
        lua_setfield(lua_state, -2, "settings");

        lua_newtable(lua_state);
        lua_pushcfunction(lua_state, LuaLocaleGetText);
        lua_setfield(lua_state, -2, "get_text");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaLocaleSetLanguage, 1);
        lua_setfield(lua_state, -2, "set_language");
        lua_pushcfunction(lua_state, LuaLocaleGetLanguage);
        lua_setfield(lua_state, -2, "get_language");
        lua_pushcfunction(lua_state, LuaLocaleGetAvailableLanguages);
        lua_setfield(lua_state, -2, "get_available_languages");
        lua_setfield(lua_state, -2, "locale");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourceHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourceAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourcePlay, 1);
        lua_setfield(lua_state, -2, "play");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourceStop, 1);
        lua_setfield(lua_state, -2, "stop");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourceIsPlaying, 1);
        lua_setfield(lua_state, -2, "is_playing");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSourceSetVolume, 1);
        lua_setfield(lua_state, -2, "set_volume");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioPlayOneShot, 1);
        lua_setfield(lua_state, -2, "play_oneshot");
        lua_setfield(lua_state, -2, "audio_source");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioListenerHas, 1);
        lua_setfield(lua_state, -2, "has");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioListenerAdd, 1);
        lua_setfield(lua_state, -2, "add");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioListenerIsEnabled, 1);
        lua_setfield(lua_state, -2, "is_enabled");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioListenerSetEnabled, 1);
        lua_setfield(lua_state, -2, "set_enabled");
        lua_setfield(lua_state, -2, "audio_listener");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSetMasterVolume, 1);
        lua_setfield(lua_state, -2, "set_master_volume");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioGetMasterVolume, 1);
        lua_setfield(lua_state, -2, "get_master_volume");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioSetSubmixVolume, 1);
        lua_setfield(lua_state, -2, "set_submix_volume");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaAudioGetSubmixVolume, 1);
        lua_setfield(lua_state, -2, "get_submix_volume");
        lua_setfield(lua_state, -2, "audio");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEventSubscribe, 1);
        lua_setfield(lua_state, -2, "subscribe");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEventPost, 1);
        lua_setfield(lua_state, -2, "post");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaEventFire, 1);
        lua_setfield(lua_state, -2, "fire");
        lua_setfield(lua_state, -2, "event");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataGetString, 1);
        lua_setfield(lua_state, -2, "get_string");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataSetString, 1);
        lua_setfield(lua_state, -2, "set_string");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataGetInt, 1);
        lua_setfield(lua_state, -2, "get_int");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataSetInt, 1);
        lua_setfield(lua_state, -2, "set_int");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataGetFloat, 1);
        lua_setfield(lua_state, -2, "get_float");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataSetFloat, 1);
        lua_setfield(lua_state, -2, "set_float");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataGetBool, 1);
        lua_setfield(lua_state, -2, "get_bool");
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaGameDataSetBool, 1);
        lua_setfield(lua_state, -2, "set_bool");
        lua_setfield(lua_state, -2, "game_data");

        lua_setglobal(lua_state, "won");
    }

    bool LuaScriptRuntime::LoadModule(const String& script_path, LuaScriptModule& out_module, String& out_error)
    {
        out_module = {};
        out_module.script_path = script_path;
        out_module.module_ref = lua_no_ref;
        for (uint32 i = 0; i < lua_script_builtin_function_count; ++i)
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

        for (uint32 i = 0; i < lua_script_builtin_function_count; ++i)
        {
            ScriptCallType type = static_cast<ScriptCallType>(i);
            if (!LoadFunctionRef(out_module.module_ref, GetFunctionName(type), out_module.function_refs[i], out_error))
            {
                UnloadModule(out_module);
                return false;
            }
        }

        out_error.clear();
        won::backlog::Post("[LuaScriptRuntime] script loaded: " + script_path);
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

        for (uint32 i = 0; i < lua_script_builtin_function_count; ++i)
        {
            if (module.function_refs[i] != lua_no_ref)
            {
                luaL_unref(lua_state, LUA_REGISTRYINDEX, module.function_refs[i]); // releases the cached function reference
                module.function_refs[i] = lua_no_ref;
            }
        }

        for (auto& entry : module.custom_function_refs)
        {
            if (entry.second != lua_no_ref)
            {
                luaL_unref(lua_state, LUA_REGISTRYINDEX, entry.second);
            }
        }
        module.custom_function_refs.clear();
    }

    bool LuaScriptRuntime::Call(ScriptInstanceHandle handle, const ScriptCallDesc& desc, String& out_error)
    {
        const won::function::Call* call = desc.call;
        if (call && call->output_count)
        {
            *call->output_count = 0;
        }

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

        const char* function_name = desc.function_name ? desc.function_name : "";
        int function_ref = lua_no_ref;

        if (desc.type == ScriptCallType::Custom)
        {
            if (!desc.function_name || desc.function_name[0] == '\0')
            {
                out_error = "Custom script function name is empty.";
                return false;
            }
            function_name = desc.function_name;
            auto function_it = module_it->second.custom_function_refs.find(function_name);
            if (function_it == module_it->second.custom_function_refs.end())
            {
                String load_error;
                if (!LoadFunctionRef(module_it->second.module_ref, function_name, function_ref, load_error))
                {
                    out_error = String("Custom script function was not found: ") + function_name;
                    if (!load_error.empty())
                    {
                        out_error += ". ";
                        out_error += load_error;
                    }
                    return false;
                }

                module_it->second.custom_function_refs[function_name] = function_ref;
            }
            else
            {
                function_ref = function_it->second;
            }
        }
        else
        {
            function_name = GetFunctionName(desc.type);
            function_ref = module_it->second.function_refs[GetFunctionIndex(desc.type)];
        }

        if (function_ref == lua_no_ref)
        {
			// without builtin function. this is ok
            out_error.clear();
            return true;
        }

        LuaStackGuard stack_guard(lua_state);

        lua_rawgeti(lua_state, LUA_REGISTRYINDEX, function_ref); // pushes the cached script function onto the stack       stack: [function]

        lua_rawgeti(lua_state, LUA_REGISTRYINDEX, instance_it->second.self_ref); // pushes the self table used as the first script argument       stack: [function, self]
        lua_pushinteger(lua_state, static_cast<lua_Integer>(desc.context.entity)); // pushes the entity_id       stack: [function, self, entity_id]
        lua_setfield(lua_state, -2, "entity"); // set self's "entity" field and pops the pushed entity_id       stack: [function, self.entity]

        int arg_count = 1;
        if (call && call->inputs)
        {
            for (uint32 i = 0; i < call->input_count; ++i)
            {
                const won::function::Value& input = call->inputs[i];
                switch (input.type)
                {
                case won::ValueType::Bool:
                    lua_pushboolean(lua_state, input.bool_value);
                    break;
                case won::ValueType::Int8:
                case won::ValueType::Int16:
                case won::ValueType::Int32:
                    lua_pushinteger(lua_state, static_cast<lua_Integer>(input.int32_value));
                    break;
                case won::ValueType::UInt8:
                case won::ValueType::UInt16:
                case won::ValueType::UInt32:
                    lua_pushinteger(lua_state, static_cast<lua_Integer>(input.uint32_value));
                    break;
                case won::ValueType::Int64:
                    lua_pushinteger(lua_state, static_cast<lua_Integer>(input.int64_value));
                    break;
                case won::ValueType::UInt64:
                    lua_pushinteger(lua_state, static_cast<lua_Integer>(input.uint64_value));
                    break;
                case won::ValueType::Float32:
                    lua_pushnumber(lua_state, static_cast<lua_Number>(input.float_value));
                    break;
                case won::ValueType::Float64:
                    lua_pushnumber(lua_state, static_cast<lua_Number>(input.double_value));
                    break;
                case won::ValueType::String:
                    lua_pushstring(lua_state, input.string_value ? input.string_value : "");
                    break;
                case won::ValueType::Pointer:
                    lua_pushlightuserdata(lua_state, input.pointer_value);
                    break;
                default:
                    out_error = String(function_name) + " input type is not supported.";
                    return false;
                }
                ++arg_count;
            }
        }

        ScriptCallContext previous_context = current_context;
        current_context = desc.context;
        const bool wants_outputs = call && call->outputs && call->output_capacity > 0;
        const int result_count = wants_outputs ? LUA_MULTRET : 0;
        output_strings.clear();
        if (wants_outputs)
        {
            output_strings.reserve(call->output_capacity);
        }

        if (lua_pcall(lua_state, arg_count, result_count, 0) != LUA_OK) // calls the script function and consumes the function plus its arguments
        {
            const char* error = lua_tostring(lua_state, -1);
            out_error = "Failed to call script function: ";
            out_error += function_name;
            out_error += "(";
            if (call && call->inputs)
            {
                for (uint32 i = 0; i < call->input_count; ++i)
                {
                    if (i > 0)
                    {
                        out_error += ", ";
                    }

                    switch (call->inputs[i].type)
                    {
                    case won::ValueType::Bool: out_error += "Bool"; break;
                    case won::ValueType::Int8: out_error += "Int8"; break;
                    case won::ValueType::UInt8: out_error += "UInt8"; break;
                    case won::ValueType::Int16: out_error += "Int16"; break;
                    case won::ValueType::UInt16: out_error += "UInt16"; break;
                    case won::ValueType::Int32: out_error += "Int32"; break;
                    case won::ValueType::UInt32: out_error += "UInt32"; break;
                    case won::ValueType::Int64: out_error += "Int64"; break;
                    case won::ValueType::UInt64: out_error += "UInt64"; break;
                    case won::ValueType::Float32: out_error += "Float32"; break;
                    case won::ValueType::Float64: out_error += "Float64"; break;
                    case won::ValueType::String: out_error += "String"; break;
                    case won::ValueType::Pointer: out_error += "Pointer"; break;
                    default: out_error += "Unsupported"; break;
                    }
                }
            }
            out_error += ")";
            if (error && error[0] != '\0')
            {
                out_error += ". ";
                out_error += error;
            }
            current_context = previous_context;
            return false;
        }

        current_context = previous_context;
        if (wants_outputs)
        {
            int actual_result_count = lua_gettop(lua_state) - stack_guard.top;
            if (actual_result_count < 0)
            {
                actual_result_count = 0;
            }
            uint32 readable_count = call->output_capacity;
            if (actual_result_count < static_cast<int>(readable_count))
            {
                readable_count = static_cast<uint32>(actual_result_count);
            }
            const int first_result = stack_guard.top + 1;
            uint32 written_count = 0;
            for (uint32 i = 0; i < readable_count; ++i)
            {
                won::function::Value& output = call->outputs[i];
                output = {};
                const int result_index = first_result + static_cast<int>(i);
                if (lua_isboolean(lua_state, result_index))
                {
                    output.type = won::ValueType::Bool;
                    output.bool_value = lua_toboolean(lua_state, result_index) != 0;
                }
                else if (lua_isinteger(lua_state, result_index))
                {
                    output.type = won::ValueType::Int64;
                    output.int64_value = static_cast<int64>(lua_tointeger(lua_state, result_index));
                }
                else if (lua_isnumber(lua_state, result_index))
                {
                    output.type = won::ValueType::Float64;
                    output.double_value = static_cast<double>(lua_tonumber(lua_state, result_index));
                }
                else if (lua_isstring(lua_state, result_index))
                {
                    output.type = won::ValueType::String;
                    output_strings.push_back(lua_tostring(lua_state, result_index));
                    output.string_value = output_strings.back().c_str();
                }
                else if (lua_islightuserdata(lua_state, result_index))
                {
                    output.type = won::ValueType::Pointer;
                    output.pointer_value = lua_touserdata(lua_state, result_index);
                }
                else
                {
                    output.type = won::ValueType::Unknown;
                }
                ++written_count;
            }
            if (call->output_count)
            {
                *call->output_count = written_count;
            }
        }

        out_error.clear();
        return true;
    }
}
