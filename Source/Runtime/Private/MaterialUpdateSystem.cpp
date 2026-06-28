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
        struct MaterialBucket
        {
            UnorderedSet<resource::Material*> materials;
            bool dirty = false;
        };

        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        const uint32 job_count = static_cast<uint32>(material_array->GetSize());
        Vector<MaterialBucket> material_buckets(jobsystem::DispatchGroupCount(job_count, groupsize_light));

        jobsystem::Dispatch(sub_ctx, job_count, groupsize_light, [&](jobsystem::JobArgs args) {
            MaterialBucket& bucket = material_buckets[args.group_id];
            MaterialComponent& material_comp = material_array->data[args.job_index];

            if (material_comp.IsDirty())
            {
                bucket.dirty = true;
                material_comp.SetDirty(false);
            }

            if (material_comp.material)
                bucket.materials.insert(material_comp.material.get());
        });

        jobsystem::Wait(sub_ctx);

        bool dirty = false;
        Vector<resource::Material*> unique_materials;
        Size unique_slot_sum = 0;
        {
            UnorderedSet<resource::Material*> merged;
            for (MaterialBucket& bucket : material_buckets)
            {
                dirty |= bucket.dirty;
                for (resource::Material* material : bucket.materials)
                {
                    if (merged.insert(material).second)
                    {
                        material->material_offset = (uint32)unique_slot_sum;
                        unique_materials.push_back(material);
                        unique_slot_sum += material->slots.size();
                    }
                }
            }
        }

        const bool material_structure_changed = render_data.shader_materials.size() != unique_slot_sum;
        if (!dirty && !material_structure_changed)
        {
            return;
        }

        render_data.shader_materials.resize(unique_slot_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)unique_materials.size(), groupsize_light, [&](jobsystem::JobArgs args) {
            resource::Material* material = unique_materials[args.job_index];

            for (Size i = 0; i < material->slots.size(); ++i)
            {
                const resource::MaterialSlot& material_slot = material->slots[i];
                ShaderMaterial& shader_material = render_data.shader_materials[material->material_offset + i];
                shader_material.Init();
                shader_material.base_color = pack_half4(material_slot.base_color);
                shader_material.emissive_color_metallic = pack_half4(0.f, 0.f, 0.f, material_slot.metallic);
                shader_material.roughness_reflectance_refraction_padding = pack_half4(material_slot.roughness, material_slot.reflectance, 0.f, 0.f);
                shader_material.anisotropy_sheenroughness_clearcoat_clearcoatroughness = pack_half4(material_slot.anisotropy, material_slot.sheen_roughness, material_slot.clearcoat, material_slot.clearcoat_roughness);
                shader_material.sheencolor_padding = pack_half4(material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z, 0.f);
                uint32 gpu_flags = SHADER_MATERIAL_FLAG_NONE;
                if (material_slot.double_sided) { gpu_flags |= SHADER_MATERIAL_FLAG_DOUBLE_SIDED; }
                if (material_slot.use_vertex_colors) { gpu_flags |= SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; }
                if (material_slot.receive_shadow) { gpu_flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; }
                shader_material.flags = gpu_flags;

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
