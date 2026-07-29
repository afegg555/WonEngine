#include "BehaviorTreeSystem.h"

#include "Backlog.h"
#include "Scene.h"

namespace won::ecs
{
    namespace
    {
        void AbortSubtree(BehaviorTreeComponent& tree, int32 index, int32 abort_action_node)
        {
            const ai::BehaviorTreeNode& node = tree.tree->nodes[static_cast<Size>(index)];

            if (node.type == ai::BehaviorTreeNode::Type::Action)
            {
                if (index == abort_action_node)
                {
                    if (tree.running_action_node == index)
                    {
                        tree.running_action_node = -1;
                    }
                    if (tree.pending_action_node == index)
                    {
                        tree.pending_action_node = -1;
                    }
                    if (tree.action_result_node == index)
                    {
                        tree.action_result_node = -1;
                    }
                    tree.pending_abort_node = index;
                }
                return;
            }

            for (int32 child = node.first_child; child < node.first_child + node.child_count; ++child)
            {
                AbortSubtree(tree, child, abort_action_node);
            }
            tree.node_cursor[static_cast<Size>(index)] = 0;
        }

        ai::BehaviorTree::Status TickNode(BehaviorTreeComponent& tree, int32 index)
        {
            const ai::BehaviorTreeNode& node = tree.tree->nodes[static_cast<Size>(index)];
            int32& cursor = tree.node_cursor[static_cast<Size>(index)];

            switch (node.type)
            {
            case ai::BehaviorTreeNode::Type::Selector:
            {
                const int32 previous_action = tree.running_action_node;
                for (int32 offset = 0; offset < node.child_count; ++offset)
                {
                    const int32 child = node.first_child + offset;
                    const ai::BehaviorTree::Status status = TickNode(tree, child);
                    if (status == ai::BehaviorTree::Status::Failure)
                    {
                        continue;
                    }

                    if (cursor != offset && cursor < node.child_count)
                    {
                        AbortSubtree(tree, node.first_child + cursor, previous_action);
                    }
                    cursor = offset;
                    return status;
                }

                cursor = 0;
                return ai::BehaviorTree::Status::Failure;
            }
            case ai::BehaviorTreeNode::Type::Sequence:
            {
                if (cursor < 0 || cursor >= node.child_count)
                {
                    cursor = 0;
                }

				// Check all guard conditions before running the sequence. If any guard fails, abort the running action and return failure.
                for (int32 offset = 0; offset < cursor; ++offset)
                {
                    const int32 guard = node.first_child + offset;
                    if (tree.tree->nodes[static_cast<Size>(guard)].type != ai::BehaviorTreeNode::Type::Condition)
                    {
                        continue;
                    }
                    if (TickNode(tree, guard) == ai::BehaviorTree::Status::Success)
                    {
                        continue;
                    }
                    AbortSubtree(tree, index, tree.running_action_node);
                    cursor = 0;
                    return ai::BehaviorTree::Status::Failure;
                }

                while (cursor < node.child_count)
                {
                    const int32 child = node.first_child + cursor;
                    const ai::BehaviorTree::Status status = TickNode(tree, child);
                    if (status == ai::BehaviorTree::Status::Running)
                    {
                        return ai::BehaviorTree::Status::Running;
                    }
                    if (status == ai::BehaviorTree::Status::Failure)
                    {
                        cursor = 0;
                        return ai::BehaviorTree::Status::Failure;
                    }
                    ++cursor;
                }

                cursor = 0;
                return ai::BehaviorTree::Status::Success;
            }
            case ai::BehaviorTreeNode::Type::Action:
            {
                if (tree.action_result_node == index)
                {
                    tree.action_result_node = -1;
                    if (tree.action_result != ai::BehaviorTree::Status::Running)
                    {
                        if (tree.running_action_node == index)
                        {
                            tree.running_action_node = -1;
                        }
                        return tree.action_result;
                    }
                }

				// keep running the action until the script reports success or failure. The script will call OnBehaviorAction and return "running" to keep it running.
                tree.pending_action_node = index;
                tree.running_action_node = index;
                return ai::BehaviorTree::Status::Running;
            }
            case ai::BehaviorTreeNode::Type::Condition:
            {
                const ai::Blackboard::Value* stored = nullptr;
                for (const ai::Blackboard& entry : tree.blackboard)
                {
                    if (entry.key == node.operand.key)
                    {
                        stored = &entry.value;
                        break;
                    }
                }

                if (!stored)
                {
                    if (!tree.missing_key_warned)
                    {
                        tree.missing_key_warned = true;
                        backlog::Post("[BehaviorTree] condition references unknown blackboard key: " + node.operand.key, backlog::LogLevel::Warning);
                    }
                    return ai::BehaviorTree::Status::Failure;
                }

                const ai::Blackboard::Value& operand = node.operand.value;
                if (stored->type != operand.type || stored->type == ai::Blackboard::Value::Type::None)
                {
                    return ai::BehaviorTree::Status::Failure;
                }

                bool passed = false;
                switch (stored->type)
                {
                case ai::Blackboard::Value::Type::Bool:
                    if (node.op == ai::BehaviorTreeNode::CompareOp::Equal)
                    {
                        passed = stored->bool_value == operand.bool_value;
                    }
                    else if (node.op == ai::BehaviorTreeNode::CompareOp::NotEqual)
                    {
                        passed = stored->bool_value != operand.bool_value;
                    }
                    break;
                case ai::Blackboard::Value::Type::Float:
                    switch (node.op)
                    {
                    case ai::BehaviorTreeNode::CompareOp::Equal: passed = stored->float_value == operand.float_value; break;
                    case ai::BehaviorTreeNode::CompareOp::NotEqual: passed = stored->float_value != operand.float_value; break;
                    case ai::BehaviorTreeNode::CompareOp::Less: passed = stored->float_value < operand.float_value; break;
                    case ai::BehaviorTreeNode::CompareOp::Greater: passed = stored->float_value > operand.float_value; break;
                    default: break;
                    }
                    break;
                case ai::Blackboard::Value::Type::String:
                    if (node.op == ai::BehaviorTreeNode::CompareOp::Equal)
                    {
                        passed = stored->string_value == operand.string_value;
                    }
                    else if (node.op == ai::BehaviorTreeNode::CompareOp::NotEqual)
                    {
                        passed = stored->string_value != operand.string_value;
                    }
                    break;
                default:
                    break;
                }
                return passed ? ai::BehaviorTree::Status::Success : ai::BehaviorTree::Status::Failure;
            }
            default:
                return ai::BehaviorTree::Status::Failure;
            }
        }
    }

    void BehaviorTreeSystem::Update(Scene& scene, float)
    {
        auto tree_array = scene.GetComponentArray<BehaviorTreeComponent>();
        if (!tree_array)
        {
            return;
        }

        jobsystem::Context ctx;
        jobsystem::Dispatch(ctx, (uint32_t)tree_array->GetSize(), jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            BehaviorTreeComponent& tree = tree_array->data[args.job_index];
            if (!tree.tree || tree.tree->nodes.empty())
            {
                return;
            }

            if (tree.node_cursor.size() != tree.tree->nodes.size())
            {
                tree.node_cursor.assign(tree.tree->nodes.size(), 0);
                tree.running_action_node = -1;
                tree.pending_action_node = -1;
                tree.pending_abort_node = -1;
                tree.action_result_node = -1;
            }

            if (!tree.IsEnabled())
            {
                AbortSubtree(tree, 0, tree.running_action_node);
                return;
            }

            TickNode(tree, 0);
        });
        jobsystem::Wait(ctx);
    }
}
