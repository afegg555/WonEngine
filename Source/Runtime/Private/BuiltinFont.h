#pragma once
#include "RHIResource.h"
#include "Types.h"

namespace won::rendering
{
    class RHIDevice;
}

namespace won::builtinfont
{
    // Engine built-in CP437 font (Modern DOS 8x16, CC0), baked into one R8 atlas.
    // 256 glyphs, each 8x16 px, packed into a 16x16 cell grid:
    //   atlas_width  = 16 cols * 8 px  = 128
    //   atlas_height = 16 rows * 16 px = 256
    // Glyph g -> cell (col = g % 16, row = g / 16); its top-left pixel = (col * 8, row * 16).
    inline constexpr int glyph_width = 8;
    inline constexpr int glyph_height = 16;
    inline constexpr int glyph_count = 256;
    inline constexpr int atlas_cols = 16;
    inline constexpr int atlas_rows = 16;
    inline constexpr int atlas_width = atlas_cols * glyph_width;
    inline constexpr int atlas_height = atlas_rows * glyph_height;

    bool BuildAtlas(rendering::RHIDevice& device);
    void Shutdown();
    bool IsReady();
    rendering::RHISubresourceHandle GetAtlasSRV();
}
