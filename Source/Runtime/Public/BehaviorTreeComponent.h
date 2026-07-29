#pragma once
#include "BehaviorTree.h"
#include "Types.h"

namespace won::ecs
{
    struct BehaviorTreeComponent
    {
        enum Flags
        {
            Empty = 0,
            Enabled = 1 << 0,
        };

        uint32 flags = Enabled;

        std::shared_ptr<ai::BehaviorTree> tree;
        String tree_asset_path;

        Vector<ai::Blackboard> blackboard;

		// These values are updated by BehaviorTreeSystem.
        Vector<int32> node_cursor; // current child index
		int32 running_action_node = -1; // updated by BehaviorTreeSystem
		int32 pending_action_node = -1; // produced by BehaviorTreeSystem, consumed by ScriptEventDispatchSystem to call OnBehaviorAction in script
		int32 pending_abort_node = -1; // produced by BehaviorTreeSystem, consumed by ScriptEventDispatchSystem to call OnBehaviorAbort in script
		int32 action_result_node = -1; // produced by ScriptEventDispatchSystem, consumed by BehaviorTreeSystem to determine the result of the action
        ai::BehaviorTree::Status action_result = ai::BehaviorTree::Status::Failure;
        bool missing_key_warned = false;
        bool invalid_result_warned = false;

        constexpr void SetEnabled(bool value = true) { if (value) { flags |= Enabled; } else { flags &= ~Enabled; } }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
    };
}
