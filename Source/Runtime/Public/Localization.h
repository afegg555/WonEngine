#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::locale
{
    inline constexpr const char* localization_directory = "Localization";
    inline constexpr const char* localization_file_extension = "json";
    inline constexpr uint32 localization_table_version = 1;

    struct TableEntry
    {
        String source;
        String text;
        String comment;
    };

    using Table = UnorderedMap<String, TableEntry>;

    WONENGINE_API String NormalizeLanguage(const String& language); // to BCP 47 format,  en_US -> en-US, en_us -> en-US, EN -> en, etc.
    WONENGINE_API bool LoadTable(const String& path, Table& out_table);
    WONENGINE_API bool SaveTable(const String& path, const String& language, const Table& table);
    WONENGINE_API String GetSystemLanguage();

    class WONENGINE_API LocaleDomain
    {
    public:
        void Initialize(const String& content_root, const Vector<String>& available_languages, const String& default_language);
        void Shutdown();

        String GetTablePath(const String& language) const;
        bool SetLanguage(const String& language);
        const String& GetLanguage() const;
        const Vector<String>& GetAvailableLanguages() const;

        const String& GetText(const String& key);
        uint32 GetRevision() const;

    private:
        String FindAvailableLanguage(const String& language) const;

        String content_root;
        String default_language;
        Vector<String> available_languages;
        String current_language;
        UnorderedMap<String, String> entries;
        UnorderedMap<String, String> fallback_entries;
        UnorderedMap<String, String> missing_entries;
        uint32 revision = 1;
    };

	// helper functions for global game domain
    WONENGINE_API LocaleDomain& GetGameDomain();
    inline void Initialize(const String& content_root, const Vector<String>& available_languages, const String& default_language)
    {
        GetGameDomain().Initialize(content_root, available_languages, default_language);
    }
    inline void Shutdown() { GetGameDomain().Shutdown(); }
    inline String GetTablePath(const String& language) { return GetGameDomain().GetTablePath(language); }
    inline bool SetLanguage(const String& language) { return GetGameDomain().SetLanguage(language); }
    inline const String& GetLanguage() { return GetGameDomain().GetLanguage(); }
    inline const Vector<String>& GetAvailableLanguages() { return GetGameDomain().GetAvailableLanguages(); }
    inline const String& GetText(const String& key) { return GetGameDomain().GetText(key); }
    inline uint32 GetRevision() { return GetGameDomain().GetRevision(); }
}
