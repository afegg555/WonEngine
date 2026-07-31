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
    WONENGINE_API String GetTablePath(const String& language);
    WONENGINE_API bool LoadTable(const String& path, Table& out_table);
    WONENGINE_API bool SaveTable(const String& path, const String& language, const Table& table);

    WONENGINE_API void Initialize(const String& content_root, const Vector<String>& available_languages, const String& default_language);
    WONENGINE_API void Shutdown();

    WONENGINE_API String GetSystemLanguage();
    WONENGINE_API bool SetLanguage(const String& language);
    WONENGINE_API const String& GetLanguage();
    WONENGINE_API const Vector<String>& GetAvailableLanguages();

    WONENGINE_API String GetText(const String& key);
    WONENGINE_API uint32 GetRevision();
}
