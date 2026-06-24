#include "RenderableUpdateSystem.h"

#include "Scene.h"
#include "JobSystem.h"
#include "MathUtils.h"

#include <iterator>

namespace won::ecs
{
    void RenderableUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct RenderableBucket
        {
            Vector<Scene::RenderData::Renderable> opaque_renderables;
            Vector<Scene::RenderData::Renderable> transparent_renderables;
            Vector<Scene::RenderData::Renderable> line_renderables;
            Vector<Scene::RenderData::Renderable> point_renderables;
        };

        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        const auto animation_array = scene.GetComponentArray<AnimationComponent>().get();

        render_data.shader_instances.resize(transform_array->GetSize());

        render_data.opaque_renderables.clear();
        render_data.transparent_renderables.clear();
        render_data.line_renderables.clear();
        render_data.point_renderables.clear();

        const uint32 job_count = static_cast<uint32>(transform_array->GetSize());
        Vector<RenderableBucket> renderable_buckets(jobsystem::DispatchGroupCount(job_count, groupsize));

        jobsystem::Dispatch(sub_ctx, job_count, groupsize, [&](jobsystem::JobArgs args) {
            RenderableBucket& bucket = renderable_buckets[args.group_id];

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
            if (animation_array && animation_array->HasData(entity))
            {
                const AnimationComponent& animation = animation_array->GetData(entity);
                if (!animation.bone_matrices.empty())
                {
                    shader_instance.bone_matrix_offset = animation.bone_matrix_offset;
                    shader_instance.bone_count = static_cast<uint32>(animation.bone_matrices.size());
                }
            }

            if (geometry_array->HasData(entity) && material_array->HasData(entity))
            {
                const GeometryComponent& geometry_comp = geometry_array->GetData(entity);
                const MaterialComponent& material_comp = material_array->GetData(entity);
                if (!geometry_comp.mesh || !material_comp.material)
                {
                    return;
                }

                const resource::Mesh::RenderData& mesh_render_data = geometry_comp.mesh->render_data;
                if (!mesh_render_data.IsValid())
                {
                    return;
                }

                const float3 world_position = math::GetPosition(transform.world_transform);
                math::AABB world_aabb;
                world_aabb.Invalidate();
                if (geometry_comp.local_bounds.IsValid())
                {
                    world_aabb = geometry_comp.local_bounds.TransformAABB(transform.world_transform);
                }

                for (Size i = 0; i < geometry_comp.mesh->submeshes.size(); ++i)
                {
                    const resource::Submesh& submesh = geometry_comp.mesh->submeshes[i];
                    if (submesh.material_slot >= material_comp.material->slots.size())
                    {
                        continue;
                    }

                    const resource::MaterialSlot& material_slot = material_comp.material->slots[submesh.material_slot];
                    Scene::RenderData::Renderable renderable = {};
                    ObjectPushConstants& push_constants = renderable.push_constants;
                    push_constants.Init();
                    push_constants.geometry_index = geometry_comp.mesh->geometry_offset + (uint)i;
                    push_constants.material_index = material_comp.material->material_offset + submesh.material_slot;
                    push_constants.draw_offset = (uint)args.job_index;

                    renderable.index_buffer = mesh_render_data.buffer;
                    renderable.index_offset = mesh_render_data.indices.offset + submesh.first_index * sizeof(uint32);
                    renderable.index_count = submesh.index_count;
                    renderable.world_position = world_position;
                    renderable.aabb = world_aabb;
                    renderable.primitive_topology = submesh.primitive_topology;
                    renderable.shader_type = material_slot.shader_type;
                    renderable.flags = Scene::RenderData::Renderable::None;
                    if (geometry_comp.IsCastShadow())
                    {
                        renderable.flags |= Scene::RenderData::Renderable::CastShadow;
                    }
                    if ((material_slot.flags & SHADER_MATERIAL_FLAG_DOUBLE_SIDED) != 0)
                    {
                        renderable.flags |= Scene::RenderData::Renderable::DoubleSided;
                    }

                    if (submesh.primitive_topology == resource::PrimitiveTopology::LineList)
                    {
                        bucket.line_renderables.push_back(renderable);
                    }
                    else if (submesh.primitive_topology == resource::PrimitiveTopology::PointList)
                    {
                        bucket.point_renderables.push_back(renderable);
                    }
                    else if ((material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0)
                    {
                        bucket.transparent_renderables.push_back(renderable);
                    }
                    else
                    {
                        bucket.opaque_renderables.push_back(renderable);
                    }
                }
            }

        });
        jobsystem::Wait(sub_ctx);

        Size opaque_count = 0;
        Size transparent_count = 0;
        Size line_renderable_count = 0;
        Size point_renderable_count = 0;
        for (const RenderableBucket& bucket : renderable_buckets)
        {
            opaque_count += bucket.opaque_renderables.size();
            transparent_count += bucket.transparent_renderables.size();
            line_renderable_count += bucket.line_renderables.size();
            point_renderable_count += bucket.point_renderables.size();
        }

        render_data.opaque_renderables.reserve(opaque_count);
        render_data.transparent_renderables.reserve(transparent_count);
        render_data.line_renderables.reserve(line_renderable_count);
        render_data.point_renderables.reserve(point_renderable_count);
        for (RenderableBucket& bucket : renderable_buckets)
        {
            render_data.opaque_renderables.insert(render_data.opaque_renderables.end(), std::make_move_iterator(bucket.opaque_renderables.begin()), std::make_move_iterator(bucket.opaque_renderables.end()));
            render_data.transparent_renderables.insert(render_data.transparent_renderables.end(), std::make_move_iterator(bucket.transparent_renderables.begin()), std::make_move_iterator(bucket.transparent_renderables.end()));
            render_data.line_renderables.insert(render_data.line_renderables.end(), std::make_move_iterator(bucket.line_renderables.begin()), std::make_move_iterator(bucket.line_renderables.end()));
            render_data.point_renderables.insert(render_data.point_renderables.end(), std::make_move_iterator(bucket.point_renderables.begin()), std::make_move_iterator(bucket.point_renderables.end()));
        }
    }
}
