#pragma once
#include "Types.h"

namespace won::ecs
{
    struct AnimationState
    {
        String name;
		String clip; // currently only supports one clip per state. In the future, we may support multiple clips with weights.
        bool loop = true;
        float speed = 1.0f;
    };

    struct AnimationParameter
    {
        enum class Type
        {
            Bool,
            Float,
            Trigger,
        };

        String name;
        Type type = Type::Float;
        float value = 0.0f;
    };

    struct TransitionCondition
    {
        enum class Op
        {
            Greater,
            Less,
            Equal,
            NotEqual,
            IsTrue,
            IsFalse,
            TriggerSet,
        };

        String parameter;
        Op op = Op::Greater;
        float threshold = 0.0f;
    };

    struct AnimationTransition
    {
        int32 from_state = -1;
        int32 to_state = 0;
        float blend_duration = 0.2f;
        bool has_exit_time = false;
		float exit_time = 1.0f; // Normalized time (0.0 - 1.0) of the from_state animation clip. 1.0 means transition can occur at the end of the clip.
        Vector<TransitionCondition> conditions;
    };

    struct AnimationStateMachineComponent
    {
        Vector<AnimationState> states;
        Vector<AnimationParameter> parameters;
        Vector<AnimationTransition> transitions;
        int32 default_state = 0;

        int32 current_state = -1;
        bool started = false;
    };
}
