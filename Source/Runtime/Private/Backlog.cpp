#include "Backlog.h"

#include "Platform.h"
#include "FileSystem.h"

#include <deque>
#include <mutex>
#include <string>
#include <cstdio>

namespace won::backlog
{
    static bool enabled = false;
    static LogLevel log_level = LogLevel::Default;
    static std::string logfile_path;

    struct InternalState
    {
        std::deque<LogEntry> entries;
        std::mutex entries_lock;

        std::string GetText()
        {
            std::scoped_lock lock(entries_lock);
            std::string result;
            for (const auto& entry : entries)
            {
                result += entry.text;
            }
            return result;
        }

        void WriteLogFile()
        {
            std::string filename;
            if (logfile_path.empty())
            {
                filename = won::io::GetWorkingDirectory();
                if (!filename.empty() && filename.back() != '/' && filename.back() != '\\')
                {
                    filename += '/';
                }
                filename += "log.txt";
            }
            else
            {
                filename = logfile_path;
            }

            std::string text = GetText();
            won::io::WriteAllBytes(filename,
                reinterpret_cast<const uint8*>(text.data()),
                static_cast<Size>(text.size()));
        }

        ~InternalState()
        {
            WriteLogFile();
        }
    };

    static InternalState internal_state;

    void SetEnabled(bool value)
    {
        enabled = value;
    }

    bool IsEnabled()
    {
        return enabled;
    }

    std::string GetText()
    {
        return internal_state.GetText();
    }

    void Clear()
    {
        std::scoped_lock lock(internal_state.entries_lock);
        internal_state.entries.clear();
    }

    static const char* GetPrefix(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Warning:
            return "[Warning] ";
        case LogLevel::Error:
            return "[Error] ";
        default:
            return "";
        }
    }

    void Post(const char* input, LogLevel level)
    {
        if (log_level > level)
        {
            return;
        }

        std::string line = std::string(GetPrefix(level)) + input + "\n";

        LogEntry entry;
        entry.text = line;
        entry.level = level;

        {
            std::scoped_lock lock(internal_state.entries_lock);
            internal_state.entries.push_back(entry);
        }

#if defined(_WIN32)
        OutputDebugStringA(line.c_str());
#else
        fprintf(stderr, "%s", line.c_str());
#endif

        if (level >= LogLevel::Error)
        {
            internal_state.WriteLogFile();
        }
    }

    void Post(const std::string& input, LogLevel level)
    {
        Post(input.c_str(), level);
    }

    void SetLogLevel(LogLevel new_level)
    {
        log_level = new_level;
    }

    void SetLogFile(const std::string& path)
    {
        logfile_path = path;
    }
}
