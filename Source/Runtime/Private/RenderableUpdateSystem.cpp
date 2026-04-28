#include "RenderableUpdateSystem.h"

#include "Scene.h"
#include "JobSystem.h"
#include "TransformComponent.h"
#include "GeometryComponent.h"
#include "MaterialComponent.h"

namespace won::ecs
{
    void RenderableUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        render_data.shader_instances.resize(transform_array->GetSize());

        render_data.renderables.resize(render_data.shader_geometries.size());
        std::atomic<uint32> renderable_count{ 0 };

        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {

            const TransformComponent& transform = transform_array->data[args.job_index];
            ShaderInstance& shader_instance = render_data.shader_instances[args.job_index];
            shader_instance.Init();
            shader_instance.world_transform = transform.world_transform;

            XMMATRIX x_normal_mat = XMLoadFloat4x4(&transform.world_transform);
            x_normal_mat.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
            x_normal_mat = XMMatrixInverse(nullptr, x_normal_mat);
            x_normal_mat = XMMatrixTranspose(x_normal_mat);
            XMStoreFloat3x3(&shader_instance.normal_transform, x_normal_mat);

            Entity entity = transform_array->index_to_entity[args.job_index];
            if (geometry_array->HasData(entity) && material_array->HasData(entity))
            {
                const GeometryComponent& geometry_comp = geometry_array->GetData(entity);
                const MaterialComponent& material_comp = material_array->GetData(entity);
                if (!geometry_comp.mesh)
                {
                    return;
                }

                const resource::Mesh::RenderData& mesh_render_data = geometry_comp.mesh->render_data;
                if (!mesh_render_data.IsValid())
                {
                    return;
                }

                const uint32 submesh_count = static_cast<uint32>(geometry_comp.mesh->submeshes.size());
                uint32 index = renderable_count.fetch_add(submesh_count);
                for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                {
                    const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                    if (submesh.material_slot >= material_comp.material_slots.size())
                    {
                        continue;
                    }

                    const MaterialSlot& material_slot = material_comp.material_slots[submesh.material_slot];
                    Scene::RenderData::Renderable& renderable = render_data.renderables[index + i];
                    ObjectPushConstants& push_constants = renderable.push_constants;
                    push_constants.Init();
                    push_constants.geometry_index = geometry_comp.geometry_offset + (uint)i;
                    push_constants.material_index = material_comp.material_offset + submesh.material_slot;
                    push_constants.instance_index = (uint)transform_array->entity_to_index[entity];

                    renderable.index_buffer = mesh_render_data.buffer;
                    renderable.index_offset = mesh_render_data.indices.offset + submesh.first_index * sizeof(uint32);
                    renderable.index_count = submesh.index_count;
                    renderable.primitive_topology = submesh.primitive_topology;
                    renderable.flags = Scene::RenderData::Renderable::None;
                    if (geometry_comp.IsCastShadow())
                    {
                        renderable.flags |= Scene::RenderData::Renderable::CastShadow;
                    }
                    if ((material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0)
                    {
                        renderable.flags |= Scene::RenderData::Renderable::Transparent;
                    }
                }
            }

        });
        jobsystem::Wait(sub_ctx);

        render_data.renderables.resize(renderable_count.load());
    }
}
