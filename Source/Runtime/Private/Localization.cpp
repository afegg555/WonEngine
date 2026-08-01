#include "Localization.h"

#include "Backlog.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "Platform.h"
#include "SpinLock.h"
#include "StringUtils.h"

#include <algorithm>

namespace won::locale
{
    static utils::SpinLock missing_entries_lock;

    String NormalizeLanguage(const String& language)
    {
        String normalized;
        String subtag;
        Size subtag_index = 0;
        for (Size i = 0; i <= language.size(); ++i)
        {
            const char character = i < language.size() ? language[i] : '-';
            if (character != '-' && character != '_')
            {
                subtag += character;
                continue;
            }
            if (subtag.empty())
            {
                continue;
            }

            const bool all_alpha = std::all_of(subtag.begin(), subtag.end(), [](unsigned char c) { return std::isalpha(c) != 0; });
            if (subtag_index > 0 && all_alpha && subtag.size() == 4)
            {
                subtag = utils::Capitalize(subtag);
            }
            else if (subtag_index > 0 && all_alpha && subtag.size() == 2)
            {
                subtag = utils::ToUpper(subtag);
            }
            else
            {
                subtag = utils::ToLower(subtag);
            }

            if (!normalized.empty())
            {
                normalized += '-';
            }
            normalized += subtag;
            subtag.clear();
            ++subtag_index;
        }
        return normalized;
    }

    String GetSystemLanguage()
    {
#if defined(_WIN32)
        wchar_t locale_name[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetUserDefaultLocaleName(locale_name, LOCALE_NAME_MAX_LENGTH) == 0)
        {
            return String();
        }
        return utils::EncodeUtf8(locale_name);
#else
        return String();
#endif
    }

    bool LoadTable(const String& path, Table& out_table)
    {
        out_table.clear();

        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path))
        {
            return false;
        }
        if (!archive.BeginObject())
        {
            backlog::Post("[Locale] malformed language table: " + path, backlog::LogLevel::Warning);
            return false;
        }

        uint32 version = 0;
        archive.Field("version", version);
        if (version != localization_table_version)
        {
            backlog::Post("[Locale] unsupported language table version " + std::to_string(version) + ": " + path, backlog::LogLevel::Warning);
            archive.EndObject();
            return false;
        }

        if (archive.BeginObject("entries"))
        {
            const Vector<String> keys = archive.GetObjectKeys();
            out_table.reserve(keys.size());
            for (const String& key : keys)
            {
                if (!archive.BeginObject(key.c_str()))
                {
                    continue;
                }
                TableEntry entry;
                archive.Field("source", entry.source);
                archive.Field("text", entry.text);
                archive.Field("comment", entry.comment);
                archive.EndObject();
                out_table.emplace(key, std::move(entry));
            }
            archive.EndObject();
        }
        archive.EndObject();
        return true;
    }

    bool SaveTable(const String& path, const String& language, const Table& table)
    {
        Vector<String> keys;
        keys.reserve(table.size());
        for (const auto& pair : table)
        {
            keys.push_back(pair.first);
        }
        std::sort(keys.begin(), keys.end());

        serialize::JsonArchive archive(serialize::ArchiveMode::Write, { true });
        archive.BeginObject();
        archive.Field("version", localization_table_version);
        archive.Field("language", language);
        archive.BeginObject("entries");
        for (const String& key : keys)
        {
            const TableEntry& entry = table.at(key);
            archive.BeginObject(key.c_str());
            archive.Field("source", entry.source);
            archive.Field("text", entry.text);
            archive.Field("comment", entry.comment);
            archive.EndObject();
        }
        archive.EndObject();
        archive.EndObject();

        io::CreateDirectories(io::GetDirectoryFromPath(path));
        return !archive.HasError() && archive.SaveToFile(path);
    }

    void LocaleDomain::Initialize(const String& root, const Vector<String>& languages, const String& default_code)
    {
        content_root = root;
        default_language = NormalizeLanguage(default_code);
        available_languages.clear();
        available_languages.reserve(languages.size());
        for (const String& code : languages)
        {
            available_languages.push_back(NormalizeLanguage(code));
        }
        current_language.clear();
        entries.clear();
        fallback_entries.clear();
        missing_entries.clear();
        ++revision;
    }

    void LocaleDomain::Shutdown()
    {
        content_root.clear();
        default_language.clear();
        available_languages.clear();
        current_language.clear();
        entries.clear();
        fallback_entries.clear();
        missing_entries.clear();
        ++revision;
    }

    String LocaleDomain::GetTablePath(const String& language) const
    {
        return io::CombinePath(io::CombinePath(content_root, localization_directory), language + "." + localization_file_extension);
    }

    String LocaleDomain::FindAvailableLanguage(const String& language) const
    {
        String candidate = NormalizeLanguage(language);
        while (!candidate.empty())
        {
            const bool available = available_languages.empty()
                ? io::IsFile(GetTablePath(candidate))
                : std::find(available_languages.begin(), available_languages.end(), candidate) != available_languages.end();
            if (available)
            {
                return candidate;
            }
            const Size separator = candidate.rfind('-');
            if (separator == String::npos)
            {
                break;
            }
            candidate = candidate.substr(0, separator);
        }
        return default_language;
    }

    bool LocaleDomain::SetLanguage(const String& language)
    {
        if (language.empty())
        {
            return false;
        }

        const String resolved = FindAvailableLanguage(language);
        if (resolved.empty())
        {
            backlog::Post("[Locale] no language table matches '" + language + "'", backlog::LogLevel::Warning);
            return false;
        }
        if (resolved != NormalizeLanguage(language))
        {
            backlog::Post("[Locale] '" + language + "' resolved to '" + resolved + "'");
        }
        if (resolved == current_language)
        {
            return true;
        }

        Table table;
        if (!LoadTable(GetTablePath(resolved), table))
        {
            backlog::Post("[Locale] failed to load language table: " + GetTablePath(resolved), backlog::LogLevel::Warning);
            return false;
        }

        entries.clear();
        entries.reserve(table.size());
        for (auto& pair : table)
        {
            entries.emplace(pair.first, std::move(pair.second.text));
        }

        if (fallback_entries.empty() && !default_language.empty() && default_language != resolved)
        {
            Table fallback_table;
            if (LoadTable(GetTablePath(default_language), fallback_table))
            {
                fallback_entries.reserve(fallback_table.size());
                for (auto& pair : fallback_table)
                {
                    fallback_entries.emplace(pair.first, std::move(pair.second.text));
                }
            }
        }

        current_language = resolved;
        missing_entries.clear();
        ++revision;
        backlog::Post("[Locale] language set to " + current_language + " (" + std::to_string(entries.size()) + " entries)");
        return true;
    }

    const String& LocaleDomain::GetLanguage() const
    {
        return current_language;
    }

    const Vector<String>& LocaleDomain::GetAvailableLanguages() const
    {
        return available_languages;
    }

    const String& LocaleDomain::GetText(const String& key)
    {
        const auto it = entries.find(key);
        if (it != entries.end())
        {
            return it->second;
        }

        const auto fallback = fallback_entries.find(key);

        missing_entries_lock.Lock();
        const auto inserted = missing_entries.emplace(key, fallback != fallback_entries.end() ? fallback->second : key);
        const String& value = inserted.first->second;
        const bool first_report = inserted.second;
        missing_entries_lock.Unlock();

        if (first_report)
        {
            if (fallback != fallback_entries.end())
            {
                backlog::Post("[Locale] missing key '" + key + "' in '" + current_language + "', using '" + default_language + "'", backlog::LogLevel::Warning);
            }
            else
            {
                backlog::Post("[Locale] missing key '" + key + "' in '" + current_language + "'", backlog::LogLevel::Warning);
            }
        }
        return value;
    }

    uint32 LocaleDomain::GetRevision() const
    {
        return revision;
    }

    LocaleDomain& GetGameDomain()
    {
        static LocaleDomain game_domain;
        return game_domain;
    }
}
