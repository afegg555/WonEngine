#include "ScriptEventDispatchSystem.h"

#include "Scene.h"
#include "ScriptComponent.h"

namespace won::ecs
{
    ScriptEventDispatchSystem::ScriptEventDispatchSystem(script::ScriptRuntime* script_runtime_in)
        : script_runtime(script_runtime_in)
    {
    }

    void ScriptEventDispatchSystem::Update(Scene& scene, float delta_time)
    {
        const auto& events = scene.GetPhysicsWorld()->GetTriggerEvents();
        if (!script_runtime || events.empty())
        {
            return;
        }

        for (const physics::Collider3DTriggerEvent& event : events)
        {
            ScriptComponent* script_component = scene.GetComponent<ScriptComponent>(event.self);
            if (!script_component || !script_component->enabled)
            {
                continue;
            }

            script::ScriptCallContext context = {};
            context.scene = &scene;
            context.entity = event.self;

            for (const ScriptSlot& script_slot : script_component->scripts)
            {
                if (!script_slot.enabled || script_slot.script_path.empty())
                {
                    continue;
                }
                if (!script_slot.initialized || !script_slot.instance.IsValid())
                {
                    continue;
                }

                won::function::Value inputs[2] = {};
                inputs[0].type = won::ValueType::UInt64;
                inputs[0].uint64_value = static_cast<uint64>(event.other);
                won::function::Call call = { inputs, 1, nullptr, 0, nullptr };
                script::ScriptCallDesc call_desc = {};
                call_desc.context = context;
                switch (event.type)
                {
                case physics::Collider3DTriggerEventType::Enter:
                    call_desc.type = script::ScriptCallType::OnTriggerEnter3D;
                    break;
                case physics::Collider3DTriggerEventType::Stay:
                    inputs[1].type = won::ValueType::Float32;
                    inputs[1].float_value = delta_time;
                    call.input_count = 2;
                    call_desc.type = script::ScriptCallType::OnTriggerStay3D;
                    break;
                case physics::Collider3DTriggerEventType::Exit:
                    call_desc.type = script::ScriptCallType::OnTriggerExit3D;
                    break;
                default:
                    continue;
                }
                call_desc.call = &call;
                String call_error;
                script_runtime->Call(script_slot.instance, call_desc, call_error);
            }
        }
    }
}
