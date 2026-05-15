#pragma once

#include "ScriptRuntime.h"
#include "Types.h"

namespace won::ecs
{
    struct ScriptSlot
    {
        String script_path;
        bool enabled = true;

        // these values will be updated on ScriptUpdateSystem
        bool initialized = false;
        script::ScriptInstanceHandle instance;
        String last_error;
    };

    struct ScriptComponent
    {
        bool enabled = true;
        Vector<ScriptSlot> scripts;
    };

    inline bool HasScript(const ScriptComponent& component, const String& script_path)
    {
        for (const ScriptSlot& script : component.scripts)
        {
            if (script.script_path == script_path)
            {
                return true;
            }
        }

        return false;
    }
}
