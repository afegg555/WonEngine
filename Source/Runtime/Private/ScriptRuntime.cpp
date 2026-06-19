#include "ScriptRuntime.h"
#include "LuaScriptRuntime.h"

#include <memory>

namespace won::script
{
    std::shared_ptr<ScriptRuntime> CreateScriptRuntime(const ScriptRuntimeDesc& desc)
    {
        switch (desc.backend)
        {
        case ScriptBackend::Lua:
            return std::make_shared<LuaScriptRuntime>(desc);
        default:
            return nullptr;
        }
    }
}
