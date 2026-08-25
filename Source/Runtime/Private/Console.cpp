#include "Console.h"

#include "Backlog.h"
#include "Configuration.h"
#include "FileSystem.h"
#include "StringUtils.h"

#include <cerrno>
#include <cstdlib>
#include <string>

namespace won::console
{
    namespace
    {
        struct RegistryEntry
        {
            ConsoleVariable* variable = nullptr;
            ConsoleCommand* command = nullptr;
        };

        Map<String, RegistryEntry>& Registry()
        {
            static Map<String, RegistryEntry> registry;
            return registry;
        }

        bool cheats_allowed = true;

        RegistryEntry* FindEntry(StringView name)
        {
            auto& registry = Registry();
            auto it = registry.find(String(name));
            if (it != registry.end())
            {
                return &it->second;
            }
            return nullptr;
        }

        void RegisterVariable(ConsoleVariable* variable)
        {
            auto& registry = Registry();
            const String& key = variable->GetName();
            if (registry.find(key) != registry.end())
            {
                backlog::Post("[console] duplicate name ignored: " + key, backlog::LogLevel::Warning);
                return;
            }
            RegistryEntry entry;
            entry.variable = variable;
            registry[key] = entry;
        }

        void RegisterCommand(ConsoleCommand* command)
        {
            auto& registry = Registry();
            const String& key = command->GetName();
            if (registry.find(key) != registry.end())
            {
                backlog::Post("[console] duplicate name ignored: " + key, backlog::LogLevel::Warning);
                return;
            }
            RegistryEntry entry;
            entry.command = command;
            registry[key] = entry;
        }

        String ConfigFilePath(StringView app_name)
        {
            if (app_name.empty())
            {
                return String();
            }
            return io::CombinePath(io::CombinePath(io::GetSaveDirectory(String(app_name)), "Config"), "console.cfg");
        }
    }

    ConsoleVariable::ConsoleVariable(const char* name, bool default_value, const char* help, uint32 flags)
        : name(name), help(help), type(ConsoleVariableType::Bool), flags(flags), bool_value(default_value)
    {
        string_value = default_value ? "true" : "false";
        default_string = string_value;
        RegisterVariable(this);
    }

    ConsoleVariable::ConsoleVariable(const char* name, int default_value, const char* help, uint32 flags)
        : name(name), help(help), type(ConsoleVariableType::Int), flags(flags), int_value(default_value)
    {
        string_value = std::to_string(default_value);
        default_string = string_value;
        RegisterVariable(this);
    }

    ConsoleVariable::ConsoleVariable(const char* name, float default_value, const char* help, uint32 flags)
        : name(name), help(help), type(ConsoleVariableType::Float), flags(flags), float_value(default_value)
    {
        string_value = std::to_string(default_value);
        default_string = string_value;
        RegisterVariable(this);
    }

    ConsoleVariable::ConsoleVariable(const char* name, const char* default_value, const char* help, uint32 flags)
        : name(name), help(help), type(ConsoleVariableType::String), flags(flags), string_value(default_value ? default_value : "")
    {
        default_string = string_value;
        RegisterVariable(this);
    }

    bool ConsoleVariable::AssignFromString(StringView value)
    {
        const String v(value);
        switch (type)
        {
        case ConsoleVariableType::Bool:
        {
            if (v == "true" || v == "1")
            {
                bool_value = true;
            }
            else if (v == "false" || v == "0")
            {
                bool_value = false;
            }
            else
            {
                backlog::Post("[console] invalid bool value for " + name + ": " + v, backlog::LogLevel::Warning);
                return false;
            }
            string_value = bool_value ? "true" : "false";
            return true;
        }
        case ConsoleVariableType::Int:
        {
            char* end = nullptr;
            errno = 0;
            const long parsed = std::strtol(v.c_str(), &end, 10);
            if (end == v.c_str() || *end != '\0' || errno == ERANGE)
            {
                backlog::Post("[console] invalid int value for " + name + ": " + v, backlog::LogLevel::Warning);
                return false;
            }
            int_value = static_cast<int>(parsed);
            string_value = std::to_string(int_value);
            return true;
        }
        case ConsoleVariableType::Float:
        {
            char* end = nullptr;
            errno = 0;
            const float parsed = std::strtof(v.c_str(), &end);
            if (end == v.c_str() || *end != '\0' || errno == ERANGE)
            {
                backlog::Post("[console] invalid float value for " + name + ": " + v, backlog::LogLevel::Warning);
                return false;
            }
            float_value = parsed;
            string_value = std::to_string(float_value);
            return true;
        }
        case ConsoleVariableType::String:
        {
            string_value = v;
            return true;
        }
        }
        return false;
    }

    bool ConsoleVariable::SetFromString(StringView value)
    {
        if (flags & ConsoleVariableFlagReadOnly)
        {
            backlog::Post("[console] read-only, cannot set: " + name, backlog::LogLevel::Warning);
            return false;
        }
        if ((flags & ConsoleVariableFlagCheat) && !cheats_allowed)
        {
            backlog::Post("[console] cheat-protected, cannot set: " + name, backlog::LogLevel::Warning);
            return false;
        }
        return AssignFromString(value);
    }

    void ConsoleVariable::ResetToDefault()
    {
        AssignFromString(default_string);
    }

    bool ConsoleVariable::GetBool() const
    {
        return bool_value;
    }

    int ConsoleVariable::GetInt() const
    {
        return int_value;
    }

    float ConsoleVariable::GetFloat() const
    {
        return float_value;
    }

    const String& ConsoleVariable::GetString() const
    {
        return string_value;
    }

    const String& ConsoleVariable::GetName() const
    {
        return name;
    }

    const String& ConsoleVariable::GetHelp() const
    {
        return help;
    }

    const String& ConsoleVariable::GetDefaultString() const
    {
        return default_string;
    }

    ConsoleVariableType ConsoleVariable::GetType() const
    {
        return type;
    }

    uint32 ConsoleVariable::GetFlags() const
    {
        return flags;
    }

    ConsoleCommand::ConsoleCommand(const char* name, ConsoleCommandHandler handler, const char* help, uint32 flags)
        : name(name), help(help), flags(flags), handler(std::move(handler))
    {
        RegisterCommand(this);
    }

    void ConsoleCommand::Execute(const Vector<StringView>& args) const
    {
        if (handler)
        {
            handler(args);
        }
    }

    const String& ConsoleCommand::GetName() const
    {
        return name;
    }

    const String& ConsoleCommand::GetHelp() const
    {
        return help;
    }

    uint32 ConsoleCommand::GetFlags() const
    {
        return flags;
    }

    ConsoleVariable* Find(StringView name)
    {
        RegistryEntry* entry = FindEntry(name);
        if (entry)
        {
            return entry->variable;
        }
        return nullptr;
    }

    bool SetFromString(StringView name, StringView value)
    {
        ConsoleVariable* variable = Find(name);
        if (!variable)
        {
            backlog::Post("[console] unknown variable: " + String(name), backlog::LogLevel::Warning);
            return false;
        }
        return variable->SetFromString(value);
    }

    void ForEach(const std::function<void(const ConsoleVariable&)>& fn)
    {
        for (const auto& pair : Registry())
        {
            if (pair.second.variable)
            {
                fn(*pair.second.variable);
            }
        }
    }

    Vector<String> FindNamesWithPrefix(StringView prefix)
    {
        Vector<String> names;
        for (const auto& pair : Registry())
        {
            if (pair.first.compare(0, prefix.size(), prefix) == 0)
            {
                names.push_back(pair.first);
            }
        }
        return names;
    }

    void Execute(StringView line)
    {
        const Vector<String> tokens = utils::Tokenize(line);
        if (tokens.empty())
        {
            return;
        }

        RegistryEntry* entry = FindEntry(tokens[0]);
        if (!entry)
        {
            backlog::Post("[console] unknown command: " + tokens[0], backlog::LogLevel::Warning);
            return;
        }

        if (entry->variable)
        {
            ConsoleVariable* variable = entry->variable;
            if (tokens.size() == 1)
            {
                backlog::Post(variable->GetName() + " = \"" + variable->GetString() + "\" (default \"" + variable->GetDefaultString() + "\") - " + variable->GetHelp());
                return;
            }
            String value = tokens[1];
            for (Size k = 2; k < tokens.size(); ++k)
            {
                value += ' ';
                value += tokens[k];
            }
            variable->SetFromString(value);
            return;
        }

        if (entry->command)
        {
            Vector<StringView> args;
            for (Size k = 1; k < tokens.size(); ++k)
            {
                args.emplace_back(tokens[k]);
            }
            entry->command->Execute(args);
        }
    }

    void LoadConfig(StringView app_name)
    {
        const String path = ConfigFilePath(app_name);
        if (path.empty())
        {
            return;
        }
        config::Configuration configuration;
        if (!configuration.LoadFromFile(path.c_str()))
        {
            return;
        }
        const uint32 count = configuration.GetKeyCount();
        for (uint32 i = 0; i < count; ++i)
        {
            const char* key = configuration.GetKey(i);
            if (!key)
            {
                continue;
            }
            const char* value = configuration.GetString(key);
            if (value)
            {
                SetFromString(key, value);
            }
        }
    }

    void SaveConfig(StringView app_name)
    {
        const String path = ConfigFilePath(app_name);
        if (path.empty())
        {
            return;
        }
        io::CreateDirectories(io::GetDirectoryFromPath(path));
        config::Configuration configuration;
        ForEach([&configuration](const ConsoleVariable& variable)
        {
            if (variable.GetFlags() & ConsoleVariableFlagArchive)
            {
                configuration.SetString(variable.GetName().c_str(), variable.GetString().c_str());
            }
        });
        configuration.SaveToFile(path.c_str());
    }

    void ApplyCommandLine(const Vector<String>& args)
    {
		for (Size i = 1; i < args.size(); ++i) // skip args[0] which is the executable path
        {
            const String& token = args[i];
			if (token.empty() || token[0] != '+') // only process tokens starting with '+'
            {
                continue;
            }
			const String name = token.substr(1); // remove the leading '+'
            if (name.empty())
            {
                continue;
            }
            String value;
            if (i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '+')
            {
				value = args[i + 1]; // take the next token as the value if it doesn't start with '+'
                ++i;
            }
            SetFromString(name, value);
        }
    }

    void SetCheatsAllowed(bool allowed)
    {
        cheats_allowed = allowed;
    }

    bool AreCheatsAllowed()
    {
        return cheats_allowed;
    }

    namespace
    {
        ConsoleCommand command_set("set", [](const Vector<StringView>& args)
        {
            if (args.size() < 2)
            {
                backlog::Post("usage: set <name> <value>", backlog::LogLevel::Warning);
                return;
            }
            String value(args[1]);
            for (Size k = 2; k < args.size(); ++k)
            {
                value += ' ';
                value += String(args[k]);
            }
            SetFromString(args[0], value);
        }, "set a console variable: set <name> <value>", ConsoleVariableFlagNone);

        ConsoleCommand command_variable_list("cvarlist", [](const Vector<StringView>& args)
        {
            const String filter = args.empty() ? String() : String(args[0]);
            ForEach([&filter](const ConsoleVariable& variable)
            {
                if (filter.empty() || variable.GetName().find(filter) != String::npos)
                {
                    backlog::Post(variable.GetName() + " = \"" + variable.GetString() + "\" - " + variable.GetHelp());
                }
            });
        }, "list console variables: cvarlist [substr]", ConsoleVariableFlagNone);

        ConsoleCommand command_help("help", [](const Vector<StringView>& args)
        {
            if (args.empty())
            {
                backlog::Post("usage: help <name>", backlog::LogLevel::Warning);
                return;
            }
            RegistryEntry* entry = FindEntry(args[0]);
            if (entry && entry->variable)
            {
                backlog::Post(entry->variable->GetName() + ": " + entry->variable->GetHelp());
                return;
            }
            if (entry && entry->command)
            {
                backlog::Post(entry->command->GetName() + ": " + entry->command->GetHelp());
                return;
            }
            backlog::Post("[console] unknown name: " + String(args[0]), backlog::LogLevel::Warning);
        }, "show help for a variable: help <name>", ConsoleVariableFlagNone);
    }
}
