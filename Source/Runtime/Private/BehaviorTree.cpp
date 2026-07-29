#include "BehaviorTree.h"

namespace won::ai
{
    bool Validate(const BehaviorTree& tree, String& out_error)
    {
        out_error.clear();

        const Vector<BehaviorTreeNode>& nodes = tree.nodes;
        if (nodes.empty())
        {
            out_error = "tree has no nodes";
            return false;
        }

        Vector<uint8> visited(nodes.size(), 0);
        visited[0] = 1;

        for (Size i = 0; i < nodes.size(); ++i)
        {
            if (visited[i] == 0)
            {
                out_error = "node is unreachable from root: " + std::to_string(i);
                return false;
            }

            const BehaviorTreeNode& node = nodes[i];
            const bool is_composite = node.type == BehaviorTreeNode::Type::Selector || node.type == BehaviorTreeNode::Type::Sequence;

            if (!is_composite)
            {
                if (node.child_count != 0)
                {
                    out_error = "leaf node must not have children: " + std::to_string(i);
                    return false;
                }
                if (node.type == BehaviorTreeNode::Type::Action && node.name.empty())
                {
                    out_error = "action node must have a name: " + std::to_string(i);
                    return false;
                }
                if (node.type == BehaviorTreeNode::Type::Condition && node.operand.key.empty())
                {
                    out_error = "condition node must have a blackboard key: " + std::to_string(i);
                    return false;
                }
                continue;
            }

            if (node.child_count <= 0)
            {
                out_error = "composite node must have at least one child: " + std::to_string(i);
                return false;
            }

            const int64 first = static_cast<int64>(node.first_child);
            const int64 last = first + static_cast<int64>(node.child_count);
            if (first <= static_cast<int64>(i) || last > static_cast<int64>(nodes.size()))
            {
                out_error = "composite child range is invalid: " + std::to_string(i);
                return false;
            }

            for (int64 child = first; child < last; ++child)
            {
                if (visited[static_cast<Size>(child)] != 0)
                {
                    out_error = "node has more than one parent: " + std::to_string(child);
                    return false;
                }
                visited[static_cast<Size>(child)] = 1;
            }
        }
        return true;
    }
}
