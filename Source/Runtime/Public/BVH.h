#pragma once
#include "Primitives.h"
#include "Types.h"

#include <algorithm>
#include <limits>

namespace won::math
{
    namespace bvh
    {
        struct BVHPrimitive
        {
            AABB bounds = {};
            float3 centroid = {};
            uint32 user_data = 0;
        };

        struct BVHNode
        {
            AABB bounds = {};
            int left_index = -1;
            int right_index = -1;
            int first_primitive = 0;
            int primitive_count = 0;

            bool IsLeaf() const { return primitive_count > 0; }
        };

        struct BVHRayHit
        {
            int primitive_index = -1;
            uint32 user_data = 0;
            float distance = (std::numeric_limits<float>::max)();

            bool IsValid() const { return primitive_index >= 0; }
        };

        struct BVH
        {
            Vector<BVHNode> nodes;
            Vector<int> primitive_indices;
            Vector<BVHPrimitive> primitives;

            void Clear()
            {
                nodes.clear();
                primitive_indices.clear();
                primitives.clear();
            }

            bool IsValid() const
            {
                return !nodes.empty() && !primitive_indices.empty() && !primitives.empty();
            }

            void Build(const Vector<BVHPrimitive>& primitive_list, uint32 max_leaf_size = 4)
            {
                Clear();
                if (primitive_list.empty())
                {
                    return;
                }

                primitives = primitive_list;
                primitive_indices.resize(primitives.size());
                for (Size i = 0; i < primitive_indices.size(); ++i)
                {
                    primitive_indices[i] = static_cast<int>(i);
                }

                const uint32 leaf_size = (std::max)(max_leaf_size, 1u);
                auto get_axis_value = [](const float3& value, uint32 axis) { return axis == 0 ? value.x : (axis == 1 ? value.y : value.z); };
                auto get_longest_axis = [](const AABB& bounds)
                {
                    const float3 extent = bounds.GetExtent();
                    if (extent.x >= extent.y && extent.x >= extent.z)
                    {
                        return 0u;
                    }
                    return extent.y >= extent.z ? 1u : 2u;
                };

                nodes.reserve(primitives.size() * 2);
                auto build_node = [&](auto&& self, int primitive_begin, int primitive_end) -> int
                {
                    const int node_index = static_cast<int>(nodes.size());
                    nodes.push_back({});

                    AABB bounds = {};
                    bounds.Invalidate();
                    AABB centroid_bounds = {};
                    centroid_bounds.Invalidate();

                    for (int i = primitive_begin; i < primitive_end; ++i)
                    {
                        const BVHPrimitive& primitive = primitives[primitive_indices[i]];
                        bounds.Merge(primitive.bounds);
                        AABB centroid_aabb = {};
                        centroid_aabb.min = primitive.centroid;
                        centroid_aabb.max = primitive.centroid;
                        centroid_bounds.Merge(centroid_aabb);
                    }

                    const int primitive_count = primitive_end - primitive_begin;
                    if (primitive_count <= static_cast<int>(leaf_size) || !centroid_bounds.IsValid())
                    {
                        BVHNode node = {};
                        node.bounds = bounds;
                        node.first_primitive = primitive_begin;
                        node.primitive_count = primitive_count;
                        nodes[node_index] = node;
                        return node_index;
                    }

                    const uint32 split_axis = get_longest_axis(centroid_bounds);
                    const int primitive_mid = primitive_begin + primitive_count / 2;
                    std::nth_element(primitive_indices.begin() + primitive_begin, primitive_indices.begin() + primitive_mid, primitive_indices.begin() + primitive_end,
                        [&](int lhs, int rhs)
                        {
                            return get_axis_value(primitives[lhs].centroid, split_axis) < get_axis_value(primitives[rhs].centroid, split_axis);
                        });

                    BVHNode node = {};
                    node.bounds = bounds;
                    node.left_index = self(self, primitive_begin, primitive_mid);
                    node.right_index = self(self, primitive_mid, primitive_end);
                    nodes[node_index] = node;
                    return node_index;
                };

                build_node(build_node, 0, static_cast<int>(primitives.size()));
            }
        };

        inline BVHPrimitive MakePrimitive(const AABB& bounds, uint32 user_data = 0)
        {
            BVHPrimitive primitive = {};
            primitive.bounds = bounds;
            primitive.centroid = bounds.GetCenter();
            primitive.user_data = user_data;
            return primitive;
        }

        template<typename PrimitiveIntersect>
        inline bool IntersectClosest(const BVH& bvh, const Ray& ray, float min_distance, float max_distance, PrimitiveIntersect primitive_intersect, BVHRayHit& out_hit)
        {
            out_hit = {};
            out_hit.distance = max_distance;
            if (!bvh.IsValid())
            {
                return false;
            }

            Vector<int> node_stack;
            node_stack.reserve(64);
            node_stack.push_back(0);

            while (!node_stack.empty())
            {
                const int node_index = node_stack.back();
                node_stack.pop_back();

                const BVHNode& node = bvh.nodes[node_index];
                float node_distance = 0.0f;
                if (!node.bounds.IntersectAABB(ray, min_distance, out_hit.distance, node_distance))
                {
                    continue;
                }

                if (node.IsLeaf())
                {
                    for (int i = 0; i < node.primitive_count; ++i)
                    {
                        const int primitive_index = bvh.primitive_indices[node.first_primitive + i];
                        const BVHPrimitive& primitive = bvh.primitives[primitive_index];
                        float primitive_distance = out_hit.distance;
                        if (primitive_intersect(primitive, min_distance, out_hit.distance, primitive_distance))
                        {
                            out_hit.primitive_index = primitive_index;
                            out_hit.user_data = primitive.user_data;
                            out_hit.distance = primitive_distance;
                        }
                    }
                    continue;
                }

                if (node.left_index >= 0)
                {
                    node_stack.push_back(node.left_index);
                }
                if (node.right_index >= 0)
                {
                    node_stack.push_back(node.right_index);
                }
            }

            return out_hit.IsValid();
        }

        inline bool IntersectClosestBounds(const BVH& bvh, const Ray& ray, float min_distance, float max_distance, BVHRayHit& out_hit)
        {
            return IntersectClosest(bvh, ray, min_distance, max_distance,
                [&](const BVHPrimitive& primitive, float primitive_min_distance, float primitive_max_distance, float& out_distance)
                {
                    return primitive.bounds.IntersectAABB(ray, primitive_min_distance, primitive_max_distance, out_distance);
                }, out_hit);
        }
    }
}
