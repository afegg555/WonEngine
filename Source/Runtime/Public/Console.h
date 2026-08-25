#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <functional>

namespace won::console
{
    enum class ConsoleVariableType : uint8
    {
        Bool,
        Int,
        Float,
        String,
    };

    enum ConsoleVariableFlags : uint32
    {
        ConsoleVariableFlagNone = 0,
		ConsoleVariableFlagCheat = 1 << 0, // can only be changed when cheats are allowed
		ConsoleVariableFlagArchive = 1 << 1, // saved to config file
		ConsoleVariableFlagReadOnly = 1 << 2, // cannot be changed at runtime
    };

    // A developer tunable declared next to the code that reads it (live-bound, no key lookup), changed at runtime via console commands or launch args.
	// usage example(use static for lifetime):
    // static console::ConsoleVariable r_shadow_resolution("r.shadow.resolution", 2048, "directional shadow map resolution", console::ConsoleVariableFlagArchive);

    class WONENGINE_API ConsoleVariable
    {
    public:
        ConsoleVariable(const char* name, bool default_value, const char* help, uint32 flags = ConsoleVariableFlagNone);
        ConsoleVariable(const char* name, int default_value, const char* help, uint32 flags = ConsoleVariableFlagNone);
        ConsoleVariable(const char* name, float default_value, const char* help, uint32 flags = ConsoleVariableFlagNone);
        ConsoleVariable(const char* name, const char* default_value, const char* help, uint32 flags = ConsoleVariableFlagNone);

        ConsoleVariable(const ConsoleVariable&) = delete;
        ConsoleVariable& operator=(const ConsoleVariable&) = delete;

        bool GetBool() const;
        int GetInt() const;
        float GetFloat() const;
        const String& GetString() const;

        bool SetFromString(StringView value);
        void ResetToDefault();

        const String& GetName() const;
        const String& GetHelp() const;
        const String& GetDefaultString() const;
        ConsoleVariableType GetType() const;
        uint32 GetFlags() const;

    private:
        bool AssignFromString(StringView value);

        String name;
        String help;
        String default_string;
        ConsoleVariableType type;
        uint32 flags;
        bool bool_value = false;
        int int_value = 0;
        float float_value = 0.0f;
        String string_value;
    };

    using ConsoleCommandHandler = std::function<void(const Vector<StringView>& args)>;

    // A named console action (a verb, not a value); registers itself on construction like a ConsoleVariable.
    // usage example(use static for lifetime):
    // static console::ConsoleCommand g_spawn("spawn", [](const won::Vector<won::StringView>& args) { /* args[0] = entity name ... */ }, "spawn an entity: spawn <name>");
    class WONENGINE_API ConsoleCommand
    {
    public:
        ConsoleCommand(const char* name, ConsoleCommandHandler handler, const char* help, uint32 flags = ConsoleVariableFlagNone);

        ConsoleCommand(const ConsoleCommand&) = delete;
        ConsoleCommand& operator=(const ConsoleCommand&) = delete;

        void Execute(const Vector<StringView>& args) const;

        const String& GetName() const;
        const String& GetHelp() const;
        uint32 GetFlags() const;

    private:
        String name;
        String help;
        uint32 flags;
        ConsoleCommandHandler handler;
    };

    WONENGINE_API ConsoleVariable* Find(StringView name);
    WONENGINE_API bool SetFromString(StringView name, StringView value);
    WONENGINE_API void ForEach(const std::function<void(const ConsoleVariable&)>& fn);
    // Registered variable and command names starting with prefix, in sorted order.
    WONENGINE_API Vector<String> FindNamesWithPrefix(StringView prefix);

    WONENGINE_API void Execute(StringView line);

    WONENGINE_API void LoadConfig(StringView app_name);
    WONENGINE_API void SaveConfig(StringView app_name);
    WONENGINE_API void ApplyCommandLine(const Vector<String>& args);

    WONENGINE_API void SetCheatsAllowed(bool allowed);
    WONENGINE_API bool AreCheatsAllowed();
}
