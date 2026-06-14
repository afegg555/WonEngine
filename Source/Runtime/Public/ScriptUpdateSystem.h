#pragma once

#include "ScriptRuntime.h"
#include "System.h"

namespace won::ecs
{
    class WONENGINE_API ScriptUpdateSystem : public System
    {
    public:
        explicit ScriptUpdateSystem(script::ScriptRuntime* script_runtime);

        ComponentMask GetReadMask() const override { return script_component_mask; }
        ComponentMask GetWriteMask() const override { return script_component_mask | transform_component_mask | name_component_mask; }
        SystemExecutionPolicy GetExecutionPolicy() const override { return SystemExecutionPolicy::Synchronous; }
        void Update(Scene& scene, float delta_time) override;

    private:
        script::ScriptRuntime* script_runtime = nullptr;
    };
}
