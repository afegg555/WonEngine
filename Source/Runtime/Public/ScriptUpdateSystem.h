#pragma once

#include "ScriptRuntime.h"
#include "System.h"

namespace won::ecs
{
    class WONENGINE_API ScriptUpdateSystem : public System
    {
    public:
        explicit ScriptUpdateSystem(script::ScriptRuntime* script_runtime);

        ComponentMask GetReadMask() const override;
        ComponentMask GetWriteMask() const override;
        SystemExecutionPolicy GetExecutionPolicy() const override;
        void Update(Scene& scene, float delta_time) override;

    private:
        script::ScriptRuntime* script_runtime = nullptr;
    };
}
