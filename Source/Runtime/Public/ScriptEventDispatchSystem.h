#pragma once

#include "ScriptRuntime.h"
#include "System.h"

namespace won::ecs
{
    class WONENGINE_API ScriptEventDispatchSystem final : public System
    {
    public:
        explicit ScriptEventDispatchSystem(script::ScriptRuntime* script_runtime);

        ComponentMask GetReadOnlyMask() const override { return script_component_mask | collider_3d_component_mask; }
        ComponentMask GetWriteMask() const override { return none_component_mask; }
        SystemExecutionPolicy GetExecutionPolicy() const override { return SystemExecutionPolicy::Synchronous; }
        void Update(Scene& scene, float delta_time) override;

    private:
        script::ScriptRuntime* script_runtime = nullptr;
    };
}
