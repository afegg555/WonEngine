#include "Font.h"
#include "FileSystem.h"
#include "RectPacker.h"

#include <mutex>
#include <cstring>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

namespace won::resource
{
    namespace
    {
        std::mutex font_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Font>> font_cache;

        struct GlyphBitmap
        {
            Font::GlyphKey key = {};
            int32 width = 0;
            int32 height = 0;
            int32 xoff = 0;
            int32 yoff = 0;
            float advance = 0.0f;
            Vector<uint8> pixels;
        };

        String NormalizeFontCacheKey(const String& path)
        {
            return io::GetAbsolutePath(path);
        }

        std::shared_ptr<Font> LoadFontUncached(const String& path)
        {
            io::FileData file_data;
            if (!io::ReadAllBytes(path, &file_data))
            {
                return nullptr;
            }

            auto font = std::make_shared<Font>();
            font->name = path;
            font->data = std::move(file_data.bytes);
            font->font_info = std::make_shared<stbtt_fontinfo>();

            // A regular .ttf
            // file will only define one font and it always be at offset 0, so it will
            // return '0' for index 0, and -1 for all other indices.        
            const int font_offset = stbtt_GetFontOffsetForIndex(reinterpret_cast<const unsigned char*>(font->data.data()), 0);
            if (font_offset < 0 || !stbtt_InitFont(font->font_info.get(), reinterpret_cast<const unsigned char*>(font->data.data()), font_offset))
            {
                return nullptr;
            }

            stbtt_GetFontVMetrics(font->font_info.get(), &font->ascent, &font->descent, &font->line_gap);

            return font;
        }
    }

    std::shared_ptr<Font> LoadFontFile(const String& path)
    {
        if (path.empty())
        {
            return nullptr;
        }

        const String key = NormalizeFontCacheKey(path);

        {
            std::lock_guard<std::mutex> lock(font_cache_mutex);
            auto it = font_cache.find(key);
            if (it != font_cache.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }
            }
        }

        auto loaded = LoadFontUncached(path);
        if (!loaded)
        {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(font_cache_mutex);
            auto it = font_cache.find(key);
            if (it != font_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            font_cache[key] = loaded;
        }

        return loaded;
    }

    bool UpdateGlyphAtlas(Font& font)
    {
        if (!font.IsValid() || font.atlas.pending_glyphs.empty())
        {
            return false;
        }

        Vector<GlyphBitmap> glyph_bitmaps;
        glyph_bitmaps.reserve(font.atlas.pending_glyphs.size());

        for (const Font::GlyphKey& key : font.atlas.pending_glyphs)
        {
            if (key.pixel_height == 0 || font.atlas.FindGlyph(key.codepoint, key.pixel_height) != nullptr)
            {
                continue;
            }

            const int glyph_index = stbtt_FindGlyphIndex(font.font_info.get(), static_cast<int>(key.codepoint));
            if (glyph_index == 0)
            {
                continue;
            }

            const float scale = stbtt_ScaleForPixelHeight(font.font_info.get(), static_cast<float>(key.pixel_height));
            GlyphBitmap glyph_bitmap = {};
            glyph_bitmap.key = key;
            unsigned char* bitmap = stbtt_GetGlyphBitmap(
                font.font_info.get(),
                scale,
                scale,
                glyph_index,
                &glyph_bitmap.width,
                &glyph_bitmap.height,
                &glyph_bitmap.xoff,
                &glyph_bitmap.yoff);

            int advance = 0;
            int left_side_bearing = 0;
            stbtt_GetCodepointHMetrics(font.font_info.get(), static_cast<int>(key.codepoint), &advance, &left_side_bearing);
            glyph_bitmap.advance = static_cast<float>(advance) * scale;

            if (bitmap != nullptr && glyph_bitmap.width > 0 && glyph_bitmap.height > 0)
            {
                glyph_bitmap.pixels.resize(static_cast<Size>(glyph_bitmap.width) * static_cast<Size>(glyph_bitmap.height));
                std::memcpy(glyph_bitmap.pixels.data(), bitmap, glyph_bitmap.pixels.size());
            }
            stbtt_FreeBitmap(bitmap, nullptr);

            glyph_bitmaps.push_back(std::move(glyph_bitmap));
        }

        if (glyph_bitmaps.empty())
        {
            font.atlas.pending_glyphs.clear();
            return false;
        }

        rectpacker::State packer = {};
        Vector<Font::GlyphKey> rect_keys;
        rect_keys.reserve(font.atlas.glyphs.size() + glyph_bitmaps.size());
        for (const auto& glyph_pair : font.atlas.glyphs)
        {
            const Font::Glyph& glyph = glyph_pair.second;
            rectpacker::Rect rect = {};
            rect.id = static_cast<int>(packer.rects.size());
            rect.w = static_cast<stbrp_coord>(glyph.size.x) + 2;
            rect.h = static_cast<stbrp_coord>(glyph.size.y) + 2;
            packer.AddRect(rect);
            rect_keys.push_back(glyph_pair.first);
        }
        for (const GlyphBitmap& glyph_bitmap : glyph_bitmaps)
        {
            rectpacker::Rect rect = {};
            rect.id = static_cast<int>(packer.rects.size());
            rect.w = static_cast<stbrp_coord>(glyph_bitmap.width) + 2;
            rect.h = static_cast<stbrp_coord>(glyph_bitmap.height) + 2;
            packer.AddRect(rect);
            rect_keys.push_back(glyph_bitmap.key);
        }

        if (!packer.Pack(4096))
        {
            return false;
        }

        std::unordered_map<Font::GlyphKey, GlyphBitmap*, Font::GlyphKeyHasher> glyph_bitmap_lookup;
        for (GlyphBitmap& glyph_bitmap : glyph_bitmaps)
        {
            glyph_bitmap_lookup[glyph_bitmap.key] = &glyph_bitmap;
        }

        const int32 old_atlas_width = font.atlas.width;
        const int32 old_atlas_height = font.atlas.height;
        Vector<uint8> old_atlas_pixels = font.atlas.pixels;
        Vector<uint8> atlas_pixels(static_cast<Size>(packer.width) * static_cast<Size>(packer.height));
        std::unordered_map<Font::GlyphKey, Font::Glyph, Font::GlyphKeyHasher> glyphs = font.atlas.glyphs;
        const float atlas_width_rcp = 1.0f / static_cast<float>(packer.width);
        const float atlas_height_rcp = 1.0f / static_cast<float>(packer.height);

        for (rectpacker::Rect rect : packer.rects)
        {
            if (rect.was_packed == 0)
            {
                continue;
            }

            if (rect.id < 0 || static_cast<Size>(rect.id) >= rect_keys.size())
            {
                continue;
            }

            const Font::GlyphKey key = rect_keys[rect.id];
            const int32 x = rect.x + 1;
            const int32 y = rect.y + 1;
            const int32 width = (std::max)(0, static_cast<int32>(rect.w) - 2);
            const int32 height = (std::max)(0, static_cast<int32>(rect.h) - 2);

            auto bitmap_it = glyph_bitmap_lookup.find(key);
            if (bitmap_it != glyph_bitmap_lookup.end())
            {
                const GlyphBitmap& glyph_bitmap = *bitmap_it->second;
                for (int32 row = 0; row < glyph_bitmap.height; ++row)
                {
                    uint8* dst = atlas_pixels.data() + static_cast<Size>(x) + static_cast<Size>(y + row) * static_cast<Size>(packer.width);
                    const uint8* src = glyph_bitmap.pixels.data() + static_cast<Size>(row) * static_cast<Size>(glyph_bitmap.width);
                    std::memcpy(dst, src, static_cast<Size>(glyph_bitmap.width));
                }

                Font::Glyph glyph = {};
                glyph.key = key;
                glyph.size = { static_cast<float>(glyph_bitmap.width), static_cast<float>(glyph_bitmap.height) };
                glyph.offset = { static_cast<float>(glyph_bitmap.xoff), static_cast<float>(glyph_bitmap.yoff) };
                glyph.advance = glyph_bitmap.advance;
                glyph.uv_min = { static_cast<float>(x) * atlas_width_rcp, static_cast<float>(y) * atlas_height_rcp };
                glyph.uv_max = { static_cast<float>(x + glyph_bitmap.width) * atlas_width_rcp, static_cast<float>(y + glyph_bitmap.height) * atlas_height_rcp };
                glyphs[key] = glyph;
            }
            else
            {
                auto glyph_it = glyphs.find(key);
                if (glyph_it != glyphs.end())
                {
                    Font::Glyph& glyph = glyph_it->second;
                    const int32 src_x = static_cast<int32>(glyph.uv_min.x * static_cast<float>(old_atlas_width));
                    const int32 src_y = static_cast<int32>(glyph.uv_min.y * static_cast<float>(old_atlas_height));
                    const int32 src_width = static_cast<int32>(glyph.size.x);
                    const int32 src_height = static_cast<int32>(glyph.size.y);
                    for (int32 row = 0; row < src_height; ++row)
                    {
                        uint8* dst = atlas_pixels.data() + static_cast<Size>(x) + static_cast<Size>(y + row) * static_cast<Size>(packer.width);
                        const uint8* src = old_atlas_pixels.data() + static_cast<Size>(src_x) + static_cast<Size>(src_y + row) * static_cast<Size>(old_atlas_width);
                        std::memcpy(dst, src, static_cast<Size>(src_width));
                    }
                    glyph.uv_min = { static_cast<float>(x) * atlas_width_rcp, static_cast<float>(y) * atlas_height_rcp };
                    glyph.uv_max = { static_cast<float>(x + width) * atlas_width_rcp, static_cast<float>(y + height) * atlas_height_rcp };
                }
            }
        }

        font.atlas.width = packer.width;
        font.atlas.height = packer.height;
        font.atlas.pixels = std::move(atlas_pixels);
        font.atlas.glyphs = std::move(glyphs);
        font.atlas.pending_glyphs.clear();
        font.atlas.dirty = true;
        font.ClearRenderData();
        return true;
    }

    void ClearFontCache()
    {
        std::lock_guard<std::mutex> lock(font_cache_mutex);
        font_cache.clear();
    }

    Size GetFontCacheSize()
    {
        std::lock_guard<std::mutex> lock(font_cache_mutex);
        return font_cache.size();
    }
}
