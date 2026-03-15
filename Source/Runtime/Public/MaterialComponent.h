#pragma once
#include "MathTypes.h"
#include "Types.h"
#include "ShaderInterop_Renderer.h"
#include "RHIResource.h"

using namespace won::rendering;

namespace won::ecs
{
    struct MaterialSlot
    {
        uint32 flags = SHADER_MATERIAL_FLAG_NONE;
        uint32 shader_type = SHADER_MATERIAL_TYPE_UNLIT;

        inline static const std::vector<std::string> shader_defines[] = {
            {"UNLIT"}, // SHADER_MATERIAL_TYPE_UNLIT,
        };
        static_assert(SHADER_MATERIAL_TYPE_COUNT == arraysize(shader_defines), "These values must match!");

        float4 base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 1.0f;
        float roughness = 0.0f;
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
            String name = "";
            std::shared_ptr<RHIResource> texture = nullptr;
            RHISubresourceHandle res_handle;

            bool IsValid() const
            {
                return texture != nullptr && res_handle.IsValid();
            }
        };
        TextureMap textures[TEXTURESLOT_COUNT];
    };

    struct MaterialComponent
    {
        Vector<MaterialSlot> material_slots = {};

        uint32 material_offset = 0; // internal usage
        MaterialSlot& GetMaterialSlot(uint32 slot_index = 0u)
        {
            assert(slot_index < material_slots.size());

            return material_slots[slot_index];
        }
        MaterialSlot& AddMaterialSlot()
        {
            material_slots.push_back({});
            return material_slots.back();
        }
        Size GetMaterialSlotCount() const
        {
            return material_slots.size();
        }
    };
}
