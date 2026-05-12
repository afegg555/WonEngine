#pragma once
#include "RHIResource.h"
#include "Resource.h"
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>
#include <unordered_set>

struct stbtt_fontinfo;

namespace won::resource
{
    struct Font : public Resource
    {
        struct GlyphKey
        {
            uint32 codepoint = 0;
            uint32 pixel_height = 0;

            bool operator==(const GlyphKey& other) const
            {
                return codepoint == other.codepoint && pixel_height == other.pixel_height;
            }
        };

        struct GlyphKeyHasher
        {
            Size operator()(const GlyphKey& key) const
            {
                return (static_cast<Size>(key.codepoint) << 32) ^ static_cast<Size>(key.pixel_height);
            }
        };

        struct Glyph
        {
            GlyphKey key = {};
            float2 size = {};
            float2 offset = {};
            float2 uv_min = {};
            float2 uv_max = {};
            float advance = 0.0f;
        };

        struct Atlas
        {
            int32 width = 0;
            int32 height = 0;
            Vector<uint8> pixels;
            std::unordered_map<GlyphKey, Glyph, GlyphKeyHasher> glyphs;
            std::unordered_set<GlyphKey, GlyphKeyHasher> pending_glyphs;
            bool dirty = true;

            bool IsValid() const
            {
                return width > 0 && height > 0 && !pixels.empty();
            }

            const Glyph* FindGlyph(uint32 codepoint, uint32 pixel_height) const
            {
                auto it = glyphs.find({ codepoint, pixel_height });
                return it != glyphs.end() ? &it->second : nullptr;
            }

            bool RequestGlyph(uint32 codepoint, uint32 pixel_height)
            {
                if (FindGlyph(codepoint, pixel_height) != nullptr)
                {
                    return false;
                }
                return pending_glyphs.insert({ codepoint, pixel_height }).second;
            }
        };

        struct RenderData
        {
            std::shared_ptr<rendering::RHIResource> atlas_texture;
            rendering::RHISubresourceHandle atlas_srv = {};
            int32 atlas_width = 0;
            int32 atlas_height = 0;

            bool IsValid() const
            {
                return atlas_texture != nullptr && atlas_srv.IsValid();
            }
        };

        Vector<uint8> data;
        std::shared_ptr<stbtt_fontinfo> font_info;
        int32 ascent = 0;
        int32 descent = 0;
        int32 line_gap = 0;
        Atlas atlas = {};
        RenderData render_data = {};

        bool IsValid() const override
        {
            return !data.empty() && font_info != nullptr;
        }

        void ClearRenderData()
        {
            render_data = {};
        }

    };

    WONENGINE_API std::shared_ptr<Font> LoadFontFile(const String& path);
    WONENGINE_API bool UpdateGlyphAtlas(Font& font);

    WONENGINE_API void ClearFontCache();
    WONENGINE_API Size GetFontCacheSize();
}
