#include "MaterialUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include "MaterialComponent.h"

using namespace won::math;
namespace won::ecs
{
    void MaterialUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        // compute prefix sum
        Size material_slot_sum = 0;
        for (Size i = 0; i < material_array->GetSize(); ++i)
        {
            MaterialComponent& material_comp = material_array->data[i];
            material_comp.material_offset = (uint32)material_slot_sum;
            material_slot_sum += material_array->data[i].GetMaterialSlotCount();
        }

        render_data.shader_materials.resize(material_slot_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)material_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const MaterialComponent& material_comp = material_array->data[args.job_index];

            for (size_t i = 0; i < material_comp.GetMaterialSlotCount(); ++i)
            {
                const MaterialSlot& material_slot = material_comp.material_slots[i];
                ShaderMaterial& shader_material = render_data.shader_materials[material_comp.material_offset + i];
                shader_material.Init();
                shader_material.base_color = pack_half4(material_slot.base_color);
                shader_material.emissive_color_metallic = pack_half4(0.f, 0.f, 0.f, material_slot.metallic);
                shader_material.roughness_reflectance_refraction_padding = pack_half4(material_slot.roughness, material_slot.reflectance, 0.f, 0.f);
                shader_material.anisotropy_sheenroughness_clearcoat_clearcoatroughness = pack_half4(material_slot.anisotropy, material_slot.sheen_roughness, material_slot.clearcoat, material_slot.clearcoat_roughness);
                shader_material.sheencolor_padding = pack_half4(material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z, 0.f);
                shader_material.flags = material_slot.flags;

                for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
                {
                    if (material_slot.textures[texture_slot].IsValid())
                    {
                        shader_material.textures[texture_slot].texture_descriptor = material_slot.textures[texture_slot].res_handle.descriptor_index;
                    }
                }
            }
        });

        jobsystem::Wait(sub_ctx);
    }
}
