#include "LuaScriptRuntime.h"

#include "Backlog.h"
#include "EventHandler.h"
#include "GameData.h"
#include "Input.h"
#include "Scene.h"
#include "SceneComponents.h"
#include "Sound.h"

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
        audio_mixer = desc.audio_mixer;
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

        auto sound = resource::LoadSoundFile(path);
        if (!sound || !sound->IsValid())
        {
            won::backlog::Post(String("[Audio] play_oneshot: sound not found: ") + path, won::backlog::LogLevel::Warning);
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
        lua_pushcclosure(lua_state, LuaMaterialFork, 1);
        lua_setfield(lua_state, -2, "fork");
        lua_setfield(lua_state, -2, "material");

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
        lua_setfield(lua_state, -2, "input");

        lua_newtable(lua_state);
        lua_pushlightuserdata(lua_state, this);
        lua_pushcclosure(lua_state, LuaSceneFindByName, 1);
        lua_setfield(lua_state, -2, "find_by_name");
        lua_setfield(lua_state, -2, "scene");

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
