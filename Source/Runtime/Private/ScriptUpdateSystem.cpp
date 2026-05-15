#include "ScriptUpdateSystem.h"

#include "Scene.h"
#include "ScriptComponent.h"

namespace won::ecs
{
    ScriptUpdateSystem::ScriptUpdateSystem(script::ScriptRuntime* script_runtime_in)
        : script_runtime(script_runtime_in)
    {
    }

    ComponentMask ScriptUpdateSystem::GetReadMask() const
    {
        return script_component_mask;
    }

    ComponentMask ScriptUpdateSystem::GetWriteMask() const
    {
        return script_component_mask | transform_component_mask | name_component_mask;
    }

    SystemExecutionPolicy ScriptUpdateSystem::GetExecutionPolicy() const
    {
        return SystemExecutionPolicy::Synchronous;
    }

    void ScriptUpdateSystem::Update(Scene& scene, float delta_time)
    {
        if (!script_runtime)
        {
            return;
        }

        auto script_array = scene.GetComponentArray<ScriptComponent>();
        if (!script_array)
        {
            return;
        }

        for (Size i = 0; i < script_array->GetSize(); ++i)
        {
            Entity entity = script_array->index_to_entity[i];
            ScriptComponent& script_component = script_array->data[i];
            if (!script_component.enabled)
            {
                continue;
            }

            script::ScriptCallContext context = {};
            context.scene = &scene;
            context.entity = entity;

            for (ScriptSlot& script_slot : script_component.scripts)
            {
                if (!script_slot.enabled || script_slot.script_path.empty())
                {
                    continue;
                }

                script::ScriptInstanceDesc desc = {};
                desc.script_path = script_slot.script_path;

                if (!script_slot.instance.IsValid())
                {
                    if (!script_runtime->CreateInstance(desc, script_slot.instance, script_slot.last_error))
                    {
                        script_slot.initialized = false;
                        continue;
                    }
                }

                if (!script_slot.initialized)
                {
                    if (!script_runtime->CallOnCreate(script_slot.instance, context, script_slot.last_error))
                    {
                        continue;
                    }

                    script_slot.initialized = true;
                }

                script_runtime->CallOnUpdate(script_slot.instance, context, delta_time, script_slot.last_error);
            }
        }
    }
}
