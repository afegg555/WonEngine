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

        Scene::RenderData& renderdata = scene.GetRenderData();
        
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
        renderdata.shader_geometry.resize(geometry_array->GetSize());

        jobsystem::Dispatch(sub_ctx, (uint32_t)geometry_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const GeometryComponent& geometry_comp = geometry_array->data[args.job_index];
            ShaderGeometry& shader_geometry = renderdata.shader_geometry[args.job_index];
            shader_geometry.position_buffer_descriptor = geometry_comp.mesh->GetRenderData()->positions.handle.descriptor_index;
            shader_geometry.normal_buffer_descriptor = geometry_comp.mesh->GetRenderData()->normals.handle.descriptor_index;
            shader_geometry.texcoord_buffer_descriptor = geometry_comp.mesh->GetRenderData()->texcoords.handle.descriptor_index;
            shader_geometry.index_buffer_descriptor = geometry_comp.mesh->GetRenderData()->indices.handle.descriptor_index;
            shader_geometry.index_count = geometry_comp.mesh->indices.size();
            });

        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        renderdata.shader_material.resize(material_array->GetSize());

        jobsystem::Dispatch(sub_ctx, (uint32_t)material_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            const MaterialComponent& material_comp = material_array->data[args.job_index];
            ShaderMaterial& shader_material = renderdata.shader_material[args.job_index];
            shader_material.base_color = material_comp.material_slots[0].base_color;
            //shader_material.emissive_color_metallic = material_comp.material_slots[0];
            // TODO : material slot binding
            });

        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        renderdata.shader_instance.resize(transform_array->GetSize());

        renderdata.renderables.resize(transform_array->GetSize()); // pre allocated size
        std::atomic<uint32> renderable_count{ 0 };

        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {

            const TransformComponent& transform = transform_array->data[args.job_index];
            ShaderInstance& shader_instance = renderdata.shader_instance[args.job_index];
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

                const Size mesh_index_buffer_offset = geometry_comp.mesh->positions.size() * sizeof(float3) + geometry_comp.mesh->normals.size() * sizeof(float3) + geometry_comp.mesh->texcoords.size() * sizeof(float2);
                const uint32 submesh_count = static_cast<uint32>(geometry_comp.mesh->submeshes.size());
                uint32 index = renderable_count.fetch_add(submesh_count);
                for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                {
                    const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                    Scene::RenderData::Renderable& renderable = renderdata.renderables[index + i];
                    ObjectPushConstants& push_constants = renderable.push_constants;
                    push_constants.geometry_index = geometry_array->entity_to_index[entity];
                    push_constants.material_index = material_array->entity_to_index[entity];
                    push_constants.instance_index = transform_array->entity_to_index[entity];

                    renderable.index_buffer = mesh_render_data->buffer;
                    renderable.index_buffer_offset = mesh_index_buffer_offset + static_cast<Size>(submesh.first_index) * sizeof(uint32);
                    renderable.index_buffer_size = static_cast<Size>(submesh.index_count) * sizeof(uint32);
                    renderable.index_count = submesh.index_count;
                }
            }

            });
        jobsystem::Wait(sub_ctx);

        renderdata.renderables.resize(renderable_count.load());
    }
}
