#include "AnimationStateMachineSystem.h"

#include "AnimationComponent.h"
#include "AnimationStateMachineComponent.h"
#include "JobSystem.h"
#include "Scene.h"

namespace won::ecs
{
    namespace
    {
        void EnterState(AnimationComponent& animation, AnimationStateMachineComponent& sm, int32 state_index, float blend_duration)
        {
            const AnimationState& state = sm.states[state_index];
            int32 clip_index = -1;
            for (Size i = 0; i < animation.clips.size(); ++i)
            {
                if (animation.clips[i] && animation.clips[i]->name == state.clip)
                {
                    clip_index = static_cast<int32>(i);
                    break;
                }
            }
            if (clip_index < 0)
            {
                return;
            }

            animation.loop = state.loop;
            animation.speed = state.speed;
            if (blend_duration > 0.0f && animation.current_clip_index != static_cast<uint32>(clip_index))
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
            animation.current_clip_index = static_cast<uint32>(clip_index);
            animation.time = 0.0f;
            animation.event_scan_time = 0.0f;
            animation.playing = true;
            animation.bone_matrices_dirty = true;
            sm.current_state = state_index;
        }

        AnimationParameter* FindParameter(AnimationStateMachineComponent& sm, const String& name)
        {
            for (AnimationParameter& parameter : sm.parameters)
            {
                if (parameter.name == name)
                {
                    return &parameter;
                }
            }
            return nullptr;
        }

        float CurrentNormalizedTime(const AnimationComponent& animation)
        {
            if (animation.current_clip_index >= animation.clips.size())
            {
                return 0.0f;
            }
            const std::shared_ptr<resource::AnimationClip>& clip = animation.clips[animation.current_clip_index];
            if (!clip)
            {
                return 0.0f;
            }
            const float duration_seconds = clip->DurationSeconds();
            return duration_seconds > 0.0f ? (animation.time / duration_seconds) : 0.0f;
        }

        bool EvaluateConditions(AnimationStateMachineComponent& sm, const AnimationTransition& transition, const AnimationComponent& animation)
        {
            if (transition.has_exit_time && CurrentNormalizedTime(animation) < transition.exit_time)
            {
                return false;
            }
            for (const TransitionCondition& condition : transition.conditions)
            {
                const AnimationParameter* parameter = FindParameter(sm, condition.parameter);
                if (!parameter)
                {
                    return false;
                }
                const float value = parameter->value;
                switch (condition.op)
                {
                case TransitionCondition::Op::Greater:    if (!(value > condition.threshold)) { return false; } break;
                case TransitionCondition::Op::Less:       if (!(value < condition.threshold)) { return false; } break;
                case TransitionCondition::Op::Equal:      if (!(value == condition.threshold)) { return false; } break;
                case TransitionCondition::Op::NotEqual:   if (!(value != condition.threshold)) { return false; } break;
                case TransitionCondition::Op::IsTrue:     if (!(value != 0.0f)) { return false; } break;
                case TransitionCondition::Op::IsFalse:    if (!(value == 0.0f)) { return false; } break;
                case TransitionCondition::Op::TriggerSet: if (!(value != 0.0f)) { return false; } break;
                }
            }
            return true;
        }

        void ConsumeTriggers(AnimationStateMachineComponent& sm, const AnimationTransition& transition)
        {
            for (const TransitionCondition& condition : transition.conditions)
            {
                if (condition.op != TransitionCondition::Op::TriggerSet)
                {
                    continue;
                }
                AnimationParameter* parameter = FindParameter(sm, condition.parameter);
                if (parameter && parameter->type == AnimationParameter::Type::Trigger)
                {
                    parameter->value = 0.0f;
                }
            }
        }
    }

    void AnimationStateMachineSystem::Update(Scene& scene, float delta_time)
    {
        auto sm_array = scene.GetComponentArray<AnimationStateMachineComponent>().get();
        auto animation_array = scene.GetComponentArray<AnimationComponent>().get();
        if (!sm_array || !animation_array)
        {
            return;
        }

        jobsystem::Context ctx;
        jobsystem::Dispatch(ctx, static_cast<uint32>(sm_array->GetSize()), jobsystem::groupsize_light, [&](jobsystem::JobArgs args) {
            AnimationStateMachineComponent& sm = sm_array->data[args.job_index];
            if (sm.states.empty())
            {
                return;
            }
            const Entity entity = sm_array->index_to_entity[args.job_index];
            if (!animation_array->HasData(entity))
            {
                return;
            }
            AnimationComponent& animation = animation_array->GetData(entity);
            if (animation.clips.empty())
            {
                return;
            }

            if (!sm.started)
            {
                if (sm.default_state >= 0 && sm.default_state < static_cast<int32>(sm.states.size()))
                {
                    EnterState(animation, sm, sm.default_state, 0.0f);
                }
                sm.started = true;
            }
            if (sm.current_state < 0 || sm.current_state >= static_cast<int32>(sm.states.size()))
            {
                return;
            }

            for (const AnimationTransition& transition : sm.transitions)
            {
                if (transition.from_state != -1 && transition.from_state != sm.current_state)
                {
                    continue;
                }
                if (transition.to_state < 0 || transition.to_state >= static_cast<int32>(sm.states.size()))
                {
                    continue;
                }
                if (transition.to_state == sm.current_state)
                {
                    continue;
                }
                if (!EvaluateConditions(sm, transition, animation))
                {
                    continue;
                }
                EnterState(animation, sm, transition.to_state, transition.blend_duration);
                ConsumeTriggers(sm, transition);
                break;
            }
        });
        jobsystem::Wait(ctx);
    }
}
