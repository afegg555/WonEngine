#include "Collider3DUpdateSystem.h"

#include "Collider3DComponent.h"
#include "JobSystem.h"
#include "Scene.h"
#include "TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace won::ecs
{
    void Collider3DUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        Collider3DWorld& collider_world = scene.GetCollider3DWorld();
        collider_world.trigger_events.clear();

        auto collider_3d_array = scene.GetComponentArray<Collider3DComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        if (!collider_3d_array || !transform_array)
        {
            collider_world.active_trigger_pairs.clear();
            return;
        }

        jobsystem::Dispatch(sub_ctx, static_cast<uint32_t>(collider_3d_array->GetSize()), groupsize, [&](jobsystem::JobArgs args) {
            const Entity entity = collider_3d_array->index_to_entity[args.job_index];
            Collider3DComponent& collider = collider_3d_array->data[args.job_index];

            collider.world_bounds.Invalidate();
            collider.world_sphere = {};
            if (!transform_array->HasData(entity))
            {
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);
            const XMMATRIX world = transform.GetWorldTransform();
            if (collider.shape_type == Collider3DComponent::Sphere)
            {
                const XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&collider.offset), world);
                const float scale_x = XMVectorGetX(XMVector3Length(world.r[0]));
                const float scale_y = XMVectorGetX(XMVector3Length(world.r[1]));
                const float scale_z = XMVectorGetX(XMVector3Length(world.r[2]));
                const float max_scale = (std::max)((std::max)(scale_x, scale_y), scale_z);

                XMStoreFloat3(&collider.world_sphere.center, center);
                collider.world_sphere.radius = (std::max)(0.0f, collider.radius) * max_scale;
                const float3 half_width = { collider.world_sphere.radius, collider.world_sphere.radius, collider.world_sphere.radius };
                collider.world_bounds.CreateFromHalfWidth(collider.world_sphere.center, half_width);
            }
            else
            {
                math::AABB local_bounds = {};
                const float3 half_extent = {
                    (std::max)(0.0f, collider.half_extent.x),
                    (std::max)(0.0f, collider.half_extent.y),
                    (std::max)(0.0f, collider.half_extent.z)
                };
                local_bounds.CreateFromHalfWidth(collider.offset, half_extent);
                collider.world_bounds = local_bounds.TransformAABB(world);

                collider.world_sphere.center = collider.world_bounds.GetCenter();
                const float3 extent = collider.world_bounds.GetExtent();
                collider.world_sphere.radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
            }

            collider.SetDirty(false);
        });

        jobsystem::Wait(sub_ctx);

        const uint32 collider_count = static_cast<uint32>(collider_3d_array->GetSize());
        Vector<Vector<Collider3DPairKey>> pair_buckets(jobsystem::DispatchGroupCount(collider_count, groupsize));

        jobsystem::Dispatch(sub_ctx, collider_count, groupsize, [&](jobsystem::JobArgs args) {
            Vector<Collider3DPairKey>& pairs = pair_buckets[args.group_id];
            const Size lhs_index = args.job_index;
            const Collider3DComponent& lhs = collider_3d_array->data[lhs_index];
            if (!lhs.IsEnabled() || !lhs.world_bounds.IsValid())
            {
                return;
            }

            const Entity lhs_entity = collider_3d_array->index_to_entity[lhs_index];
            for (Size rhs_index = lhs_index + 1; rhs_index < collider_3d_array->GetSize(); ++rhs_index)
            {
                const Collider3DComponent& rhs = collider_3d_array->data[rhs_index];
                if (!rhs.IsEnabled() || !rhs.world_bounds.IsValid())
                {
                    continue;
                }
                if (!lhs.IsTrigger() && !rhs.IsTrigger())
                {
                    continue;
                }

                bool overlap = false;
                if (lhs.shape_type == Collider3DComponent::Sphere && rhs.shape_type == Collider3DComponent::Sphere)
                {
                    const float dx = lhs.world_sphere.center.x - rhs.world_sphere.center.x;
                    const float dy = lhs.world_sphere.center.y - rhs.world_sphere.center.y;
                    const float dz = lhs.world_sphere.center.z - rhs.world_sphere.center.z;
                    const float radius_sum = lhs.world_sphere.radius + rhs.world_sphere.radius;
                    overlap = dx * dx + dy * dy + dz * dz <= radius_sum * radius_sum;
                }
                else
                {
                    const math::AABB& lhs_bounds = lhs.world_bounds;
                    const math::AABB& rhs_bounds = rhs.world_bounds;
                    overlap =
                        lhs_bounds.min.x <= rhs_bounds.max.x && lhs_bounds.max.x >= rhs_bounds.min.x &&
                        lhs_bounds.min.y <= rhs_bounds.max.y && lhs_bounds.max.y >= rhs_bounds.min.y &&
                        lhs_bounds.min.z <= rhs_bounds.max.z && lhs_bounds.max.z >= rhs_bounds.min.z;
                }

                if (!overlap)
                {
                    continue;
                }

                Collider3DPairKey pair = {};
                const Entity rhs_entity = collider_3d_array->index_to_entity[rhs_index];
                if (lhs_entity < rhs_entity)
                {
                    pair.a = lhs_entity;
                    pair.b = rhs_entity;
                }
                else
                {
                    pair.a = rhs_entity;
                    pair.b = lhs_entity;
                }
                pairs.push_back(pair);
            }
        });

        jobsystem::Wait(sub_ctx);

        Size pair_count = 0;
        for (const Vector<Collider3DPairKey>& bucket : pair_buckets)
        {
            pair_count += bucket.size();
        }

        Vector<Collider3DPairKey> current_pairs;
        current_pairs.reserve(pair_count);
        for (const Vector<Collider3DPairKey>& bucket : pair_buckets)
        {
            current_pairs.insert(current_pairs.end(), bucket.begin(), bucket.end());
        }

        Vector<Collider3DPairKey>& active_pairs = collider_world.active_trigger_pairs;
        Vector<Collider3DTriggerEvent>& events = collider_world.trigger_events;

        auto contains_pair = [](const Vector<Collider3DPairKey>& pairs, const Collider3DPairKey& pair)
        {
            for (const Collider3DPairKey& current : pairs)
            {
                if (current.a == pair.a && current.b == pair.b)
                {
                    return true;
                }
            }
            return false;
        };

        for (const Collider3DPairKey& pair : current_pairs)
        {
            const Collider3DTriggerEventType type = contains_pair(active_pairs, pair) ? Collider3DTriggerEventType::Stay : Collider3DTriggerEventType::Enter;
            events.push_back({ type, pair.a, pair.b });
            events.push_back({ type, pair.b, pair.a });
        }

        for (const Collider3DPairKey& pair : active_pairs)
        {
            if (!contains_pair(current_pairs, pair))
            {
                events.push_back({ Collider3DTriggerEventType::Exit, pair.a, pair.b });
                events.push_back({ Collider3DTriggerEventType::Exit, pair.b, pair.a });
            }
        }

        active_pairs = std::move(current_pairs);
    }
}
