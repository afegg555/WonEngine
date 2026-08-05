#include "ScriptEventDispatchSystem.h"

#include "Backlog.h"
#include "EventHandler.h"
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
        eventhandler::Dispatch();

        if (!script_runtime)
        {
            return;
        }

        const auto& events = scene.GetPhysicsWorld()->GetTriggerEvents();
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

        for (const Entity clicked : scene.GetUIClickEvents())
        {
            ScriptComponent* script_component = scene.GetComponent<ScriptComponent>(clicked);
            if (!script_component || !script_component->enabled)
            {
                continue;
            }

            script::ScriptCallContext context = {};
            context.scene = &scene;
            context.entity = clicked;

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

                script::ScriptCallDesc call_desc = {};
                call_desc.context = context;
                call_desc.type = script::ScriptCallType::OnClick;
                String call_error;
                script_runtime->Call(script_slot.instance, call_desc, call_error);
            }
        }
        for (const std::pair<Entity, String>& anim_event : scene.GetAnimationEvents())
        {
            ScriptComponent* script_component = scene.GetComponent<ScriptComponent>(anim_event.first);
            if (!script_component || !script_component->enabled)
            {
                continue;
            }

            script::ScriptCallContext context = {};
            context.scene = &scene;
            context.entity = anim_event.first;

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

                won::function::Value inputs[1] = {};
                inputs[0].type = won::ValueType::String;
                inputs[0].string_value = anim_event.second.c_str();
                won::function::Call call = { inputs, 1, nullptr, 0, nullptr };
                script::ScriptCallDesc call_desc = {};
                call_desc.context = context;
                call_desc.type = script::ScriptCallType::OnAnimationEvent;
                call_desc.call = &call;
                String call_error;
                script_runtime->Call(script_slot.instance, call_desc, call_error);
            }
        }
        for (const std::pair<Entity, String>& sequence_event : scene.GetSequenceEvents())
        {
            ScriptComponent* script_component = scene.GetComponent<ScriptComponent>(sequence_event.first);
            if (!script_component || !script_component->enabled)
            {
                continue;
            }

            script::ScriptCallContext context = {};
            context.scene = &scene;
            context.entity = sequence_event.first;

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

                won::function::Value inputs[1] = {};
                inputs[0].type = won::ValueType::String;
                inputs[0].string_value = sequence_event.second.c_str();
                won::function::Call call = { inputs, 1, nullptr, 0, nullptr };
                script::ScriptCallDesc call_desc = {};
                call_desc.context = context;
                call_desc.type = script::ScriptCallType::OnSequenceEvent;
                call_desc.call = &call;
                String call_error;
                script_runtime->Call(script_slot.instance, call_desc, call_error);
            }
        }
        scene.ClearAnimationEvents();
        scene.ClearSequenceEvents();
        scene.ClearUIClickEvents();

        if (auto tree_array = scene.GetComponentArray<BehaviorTreeComponent>())
        {
            for (Size i = 0; i < tree_array->GetSize(); ++i)
            {
                BehaviorTreeComponent& tree = tree_array->data[i];
                if (!tree.tree)
                {
                    continue;
                }
                const Entity entity = tree_array->index_to_entity[i];

                ScriptComponent* script_component = scene.GetComponent<ScriptComponent>(entity);
                if (!script_component || !script_component->enabled)
                {
                    tree.pending_abort_node = -1;
                    tree.pending_action_node = -1;
                    continue;
                }

                script::ScriptCallContext context = {};
                context.scene = &scene;
                context.entity = entity;

                if (tree.pending_abort_node >= 0)
                {
                    const int32 node_index = tree.pending_abort_node;
                    tree.pending_abort_node = -1;
                    const String node_name = tree.tree->nodes[static_cast<Size>(node_index)].name;

                    won::function::Value inputs[1] = {};
                    inputs[0].type = won::ValueType::String;
                    inputs[0].string_value = node_name.c_str();
                    won::function::Call call = { inputs, 1, nullptr, 0, nullptr };
                    script::ScriptCallDesc call_desc = {};
                    call_desc.context = context;
                    call_desc.type = script::ScriptCallType::OnBehaviorAbort;
                    call_desc.call = &call;

                    for (const ScriptSlot& script_slot : script_component->scripts)
                    {
                        if (!script_slot.enabled || !script_slot.instance.IsValid())
                        {
                            continue;
                        }
                        String call_error;
                        script_runtime->Call(script_slot.instance, call_desc, call_error);
                    }
                }

                if (tree.pending_action_node >= 0)
                {
                    const int32 node_index = tree.pending_action_node;
                    tree.pending_action_node = -1;
                    const String node_name = tree.tree->nodes[static_cast<Size>(node_index)].name;

                    won::function::Value inputs[2] = {};
                    inputs[0].type = won::ValueType::String;
                    inputs[0].string_value = node_name.c_str();
                    inputs[1].type = won::ValueType::Float32;
                    inputs[1].float_value = delta_time;
                    won::function::Value outputs[1] = {};
                    uint32 output_count = 0;
                    won::function::Call call = { inputs, 2, outputs, 1, &output_count };
                    script::ScriptCallDesc call_desc = {};
                    call_desc.context = context;
                    call_desc.type = script::ScriptCallType::OnBehaviorAction;
                    call_desc.call = &call;

                    ai::BehaviorTree::Status status = ai::BehaviorTree::Status::Failure;
                    bool has_status = false;
                    for (const ScriptSlot& script_slot : script_component->scripts)
                    {
                        if (!script_slot.enabled || !script_slot.instance.IsValid())
                        {
                            continue;
                        }
                        String call_error;
                        if (!script_runtime->Call(script_slot.instance, call_desc, call_error))
                        {
                            continue;
                        }
                        if (output_count == 0 || outputs[0].type != won::ValueType::Int64)
                        {
                            continue;
                        }
                        const int64 value = outputs[0].int64_value;
                        if (value >= 0 && value <= static_cast<int64>(ai::BehaviorTree::Status::Running))
                        {
                            status = static_cast<ai::BehaviorTree::Status>(value);
                            has_status = true;
                            break;
                        }
                    }

                    if (!has_status && !tree.invalid_result_warned)
                    {
                        tree.invalid_result_warned = true;
                        backlog::Post("[BehaviorTree] OnBehaviorAction is missing or did not return a won.ai status: " + node_name, backlog::LogLevel::Warning);
                    }
                    tree.action_result_node = node_index;
                    tree.action_result = status;
                }
            }
        }
    }
}
