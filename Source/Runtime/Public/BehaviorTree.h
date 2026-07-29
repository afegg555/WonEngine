#pragma once
#include "Resource.h"
#include "Types.h"

namespace won::ai
{
    struct Blackboard
    {
        struct Value
        {
            enum class Type : uint8
            {
                None,
                Bool,
                Float,
                String,
            };

            Type type = Type::None;
            bool bool_value = false;
            float float_value = 0.0f;
            String string_value;
        };

        String key;
        Value value;
    };

    struct BehaviorTreeNode
    {
        enum class Type : uint8
        {
            Selector,
            Sequence,
            Action,
            Condition,
        };

        enum class CompareOp : uint8
        {
            Equal,
            NotEqual,
            Less,
            Greater,
        };

        Type type = Type::Selector;
        String name;
        int32 first_child = -1;
        int32 child_count = 0;

        CompareOp op = CompareOp::Equal;
        Blackboard operand;
    };

    struct BehaviorTree : public resource::Resource
    {
        enum class Status : uint8
        {
            Success,
            Failure,
            Running,
        };

        Vector<BehaviorTreeNode> nodes;

        bool IsValid() const override { return !nodes.empty(); }
    };

    bool Validate(const BehaviorTree& tree, String& out_error);
}
