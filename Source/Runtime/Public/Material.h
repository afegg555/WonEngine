#pragma once
#include "Image.h"
#include "MathTypes.h"
#include "Resource.h"
#include "RHIResource.h"
#include "ShaderInterop_Renderer.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    enum class MaterialType : uint32
    {
        Unlit,
        PBR,
    };

    enum class MaterialBlendMode : uint32
    {
        Opaque, // dst = src.rgb, blending off so alpha is unused
        Masked, // dst = src.rgb after clip(alpha - cutoff), which disables early depth test for the draw
        Transparent, // dst = src.rgb * src.a + dst * (1 - src.a)
        Additive, // dst = src.rgb * src.a + dst, order independent so no sorting
        Premultiplied, // dst = src.rgb + dst * (1 - src.a), alpha already applied to rgb
    };

    struct MaterialSlot
    {
        MaterialType material_type = MaterialType::PBR;
        MaterialBlendMode blend_mode = MaterialBlendMode::Opaque;
        float alpha_cutoff = 0.5f;
        bool double_sided = false;
        bool use_vertex_colors = false;
        bool receive_shadow = true;

        float4 base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float3 emissive_color = { 0.0f, 0.0f, 0.0f };
        float emissive_intensity = 0.0f;
        float metallic = 0.3f;
        float roughness = 0.5f;
        float reflectance = 0.5f; // 0.5 is good enough for most dielectric materials (this means 0.04 F0)
        // Anisotropy factor. 0.0 = isotropic, 1.0 = anisotropy along tangent direction,
        // -1.0 = anisotropy along bitangent direction
        float anisotropy = 0;

        float3 sheen_color = { 1.f, 1.f, 1.f };
        float sheen_roughness = 0;
        float clearcoat = 0;
        float clearcoat_roughness = 0;

        struct TextureMap
        {
            String texture_asset_path = "";
            std::shared_ptr<Image> image = nullptr;

            bool IsValid() const
            {
                return image != nullptr && image->render_data.IsValid();
            }
        };
        TextureMap textures[TEXTURESLOT_COUNT];

        bool IsMasked() const { return blend_mode == MaterialBlendMode::Masked; }
        bool IsTransparent() const { return blend_mode >= MaterialBlendMode::Transparent; }
    };

    struct Material : public Resource
    {
        Vector<MaterialSlot> slots;
        bool dirty = false;

        bool IsValid() const override
        {
            return !slots.empty();
        }

        void SetDirty(bool value = true) { dirty = value; }
        bool IsDirty() const { return dirty; }
    };
}
