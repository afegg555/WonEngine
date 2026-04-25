#ifndef RAY_TRACE_COMMON
#define RAY_TRACE_COMMON

#include "Common.hlsli"

#define MAX_SCENE_TRACE_STACK 64

struct SceneRayHit
{
    bool hit;
    float distance;
    float3 position;
    float3 normal;
};

bool IntersectAABB(float3 origin, float3 direction, float3 bounds_min, float3 bounds_max, float min_distance, float max_distance, out float out_distance)
{
    float near_distance = min_distance;
    float far_distance = max_distance;

    [unroll]
    for (uint axis = 0; axis < 3; ++axis)
    {
        float ray_origin = origin[axis];
        float ray_direction = direction[axis];
        float box_min = bounds_min[axis];
        float box_max = bounds_max[axis];

        if (abs(ray_direction) < 0.000001f)
        {
            if (ray_origin < box_min || ray_origin > box_max)
            {
                out_distance = 0.0f;
                return false;
            }
            continue;
        }

        float inv_direction = rcp(ray_direction);
        float t0 = (box_min - ray_origin) * inv_direction;
        float t1 = (box_max - ray_origin) * inv_direction;
        if (t0 > t1)
        {
            float temp = t0;
            t0 = t1;
            t1 = temp;
        }

        near_distance = max(near_distance, t0);
        far_distance = min(far_distance, t1);
        if (near_distance > far_distance)
        {
            out_distance = 0.0f;
            return false;
        }
    }

    out_distance = near_distance;
    return true;
}

bool IntersectTriangle(float3 origin, float3 direction, float3 v0, float3 v1, float3 v2, float min_distance, float max_distance, out float out_distance, out float2 out_barycentric)
{
    float3 e1 = v1 - v0;
    float3 e2 = v2 - v0;
    float3 p = cross(direction, e2);
    float det = dot(e1, p);
    if (abs(det) < 0.000001f)
    {
        out_distance = 0.0f;
        out_barycentric = 0.0f;
        return false;
    }

    float inv_det = rcp(det);
    float3 s = origin - v0;
    float u = dot(s, p) * inv_det;
    if (u < 0.0f || u > 1.0f)
    {
        out_distance = 0.0f;
        out_barycentric = 0.0f;
        return false;
    }

    float3 q = cross(s, e1);
    float v = dot(direction, q) * inv_det;
    if (v < 0.0f || u + v > 1.0f)
    {
        out_distance = 0.0f;
        out_barycentric = 0.0f;
        return false;
    }

    float distance = dot(e2, q) * inv_det;
    if (distance < min_distance || distance > max_distance)
    {
        out_distance = 0.0f;
        out_barycentric = 0.0f;
        return false;
    }

    out_distance = distance;
    out_barycentric = float2(u, v);
    return true;
}

SceneRayHit TraceSceneRay(float3 origin, float3 direction, float min_distance, float max_distance)
{
    SceneRayHit hit;
    hit.hit = false;
    hit.distance = max_distance;
    hit.position = 0.0f;
    hit.normal = 0.0f;

    ShaderScene scene = GetScene();
    if (scene.bvh_node_buffer < 0 || scene.bvh_primitive_buffer < 0 || scene.bvh_node_count == 0)
    {
        return hit;
    }

    uint node_stack[MAX_SCENE_TRACE_STACK];
    uint stack_count = 0;
    node_stack[stack_count++] = 0;

    [loop]
    while (stack_count > 0)
    {
        uint node_index = node_stack[--stack_count];
        if (node_index >= scene.bvh_node_count)
        {
            continue;
        }

        ShaderBVHNode node = GetBVHNode(node_index);
        float node_distance = 0.0f;
        if (!IntersectAABB(origin, direction, node.bounds_min, node.bounds_max, min_distance, hit.distance, node_distance))
        {
            continue;
        }

        if (node.primitive_count > 0)
        {
            [loop]
            for (uint i = 0; i < node.primitive_count; ++i)
            {
                uint primitive_index = node.first_primitive + i;
                if (primitive_index >= scene.bvh_primitive_count)
                {
                    continue;
                }

                ShaderBVHPrimitive primitive = GetBVHPrimitive(primitive_index);
                float triangle_distance = 0.0f;
                float2 barycentric = 0.0f;
                if (IntersectTriangle(origin, direction, primitive.v0, primitive.v1, primitive.v2, min_distance, hit.distance, triangle_distance, barycentric))
                {
                    hit.hit = true;
                    hit.distance = triangle_distance;
                    hit.position = origin + direction * triangle_distance;
                    float3 normal = normalize(cross(primitive.v1 - primitive.v0, primitive.v2 - primitive.v0));
                    hit.normal = dot(normal, -direction) < 0.0f ? -normal : normal;
                }
            }
            continue;
        }

        if (node.left_index >= 0 && stack_count < MAX_SCENE_TRACE_STACK)
        {
            node_stack[stack_count++] = (uint)node.left_index;
        }
        if (node.right_index >= 0 && stack_count < MAX_SCENE_TRACE_STACK)
        {
            node_stack[stack_count++] = (uint)node.right_index;
        }
    }

    return hit;
}

#endif
