#include "RenderDataUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include "TransformComponent.h"
#include "GeometryComponent.h"
#include "MaterialComponent.h"

namespace won::ecs
{
    void RenderDataUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
        render_data.shader_geometry.resize(geometry_array->GetSize());

        jobsystem::Dispatch(sub_ctx, (uint32_t)geometry_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const GeometryComponent& geometry_comp = geometry_array->data[args.job_index];
            ShaderGeometry& shader_geometry = render_data.shader_geometry[args.job_index];
            shader_geometry.Init();
            shader_geometry.bounds_min = geometry_comp.local_bounds.min;
            shader_geometry.bounds_max = geometry_comp.local_bounds.max;

            const resource::Mesh::RenderData* mesh_render_data = geometry_comp.mesh->GetRenderData();
            if (mesh_render_data)
            {
                shader_geometry.position_buffer_descriptor = mesh_render_data->positions.handle.descriptor_index;
                shader_geometry.color_buffer_descriptor = mesh_render_data->colors.handle.descriptor_index;
                shader_geometry.normal_buffer_descriptor = mesh_render_data->normals.handle.descriptor_index;
                shader_geometry.texcoord_buffer_descriptor = mesh_render_data->texcoords.handle.descriptor_index;
                shader_geometry.tangent_buffer_descriptor = mesh_render_data->tangents.handle.descriptor_index;
                shader_geometry.index_buffer_descriptor = mesh_render_data->indices.handle.descriptor_index;
                shader_geometry.index_count = static_cast<uint32>(geometry_comp.mesh->indices.size());
            }
            });

        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        // compute prefix sum
        Size material_slot_sum = 0;
        for (Size i = 0; i < material_array->GetSize(); ++i)
        {
            MaterialComponent& material_comp = material_array->data[i];
            material_comp.material_offset = material_slot_sum;
            material_slot_sum += material_array->data[i].GetMaterialSlotCount();
        }
        render_data.shader_material.resize(material_slot_sum);

        jobsystem::Dispatch(sub_ctx, (uint32_t)material_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const MaterialComponent& material_comp = material_array->data[args.job_index];
            for (size_t i = 0; i < material_comp.GetMaterialSlotCount(); ++i)
            {
                const MaterialSlot& material_slot = material_comp.material_slots[i];
                ShaderMaterial& shader_material = render_data.shader_material[material_comp.material_offset + i];
                shader_material.Init();
                shader_material.base_color = material_slot.base_color;
                shader_material.emissive_color_metallic = float4(0.f, 0.f, 0.f, material_slot.metallic);
                shader_material.roughness_reflectance_metalness_refraction = float4(material_slot.roughness, material_slot.reflectance, material_slot.metallic, 0.f);
                shader_material.flags = material_slot.flags;

                for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
                {
                    if (material_slot.textures[i].IsValid())
                    {
                        shader_material.textures[texture_slot].texture_descriptor = material_slot.textures[i].res_handle.descriptor_index;
                    }
                    
                }
            }
            });

        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        render_data.shader_instance.resize(transform_array->GetSize());

        render_data.renderables.resize(transform_array->GetSize()); // pre allocated size
        std::atomic<uint32> renderable_count{ 0 };

        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {

            const TransformComponent& transform = transform_array->data[args.job_index];
            ShaderInstance& shader_instance = render_data.shader_instance[args.job_index];
            shader_instance.Init();
            shader_instance.local_to_world = transform.world_transform;

            Entity entity = transform_array->index_to_entity[args.job_index];
            if (geometry_array->HasData(entity) && material_array->HasData(entity))
            {
                const GeometryComponent& geometry_comp = geometry_array->GetData(entity);
                const resource::Mesh::RenderData* mesh_render_data = geometry_comp.mesh->GetRenderData();
                if (!mesh_render_data || !mesh_render_data->buffer)
                {
                    return;
                }

                const uint32 submesh_count = static_cast<uint32>(geometry_comp.mesh->submeshes.size());
                uint32 index = renderable_count.fetch_add(submesh_count);
                for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                {
                    const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                    Scene::RenderData::Renderable& renderable = render_data.renderables[index + i];
                    ObjectPushConstants& push_constants = renderable.push_constants;
                    push_constants.Init();
                    push_constants.geometry_index = geometry_array->entity_to_index[entity];
                    push_constants.material_index = material_array->entity_to_index[entity] + submesh.material_slot;
                    push_constants.instance_index = transform_array->entity_to_index[entity];

                    renderable.index_buffer = mesh_render_data->buffer;
                    renderable.index_offset = mesh_render_data->indices.offset + submesh.first_index * sizeof(uint32);
                    renderable.index_count = submesh.index_count;
                }
            }

            });
        jobsystem::Wait(sub_ctx);

        render_data.renderables.resize(renderable_count.load());
    }
}
