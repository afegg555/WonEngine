#pragma once
#include "MathTypes.h"
#include "Resource.h"
#include "RHIResource.h"
#include "ShaderInterop_Renderer.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    struct MaterialSlot
    {
        uint32 flags = SHADER_MATERIAL_FLAG_NONE;
        uint32 shader_type = SHADER_MATERIAL_TYPE_PBR;

        inline static const std::vector<std::string> shader_defines[] = {
            {"UNLIT"}, // SHADER_MATERIAL_TYPE_UNLIT,
            {}, // SHADER_MATERIAL_TYPE_PBR,
        };
        static_assert(SHADER_MATERIAL_TYPE_COUNT == arraysize(shader_defines), "These values must match!");

        float4 base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
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
            std::shared_ptr<rendering::RHIResource> texture = nullptr;
            rendering::RHISubresourceHandle res_handle;

            bool IsValid() const
            {
                return texture != nullptr && res_handle.IsValid();
            }
        };
        TextureMap textures[TEXTURESLOT_COUNT];
    };

    struct Material : public Resource
    {
        Vector<MaterialSlot> slots;
        uint32 material_offset = 0; // assigned by MaterialUpdateSystem, shared by all entities using this material

        bool IsValid() const override
        {
            return !slots.empty();
        }
    };
}
