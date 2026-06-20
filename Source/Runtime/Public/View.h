#pragma once
#include "Scene.h"
#include "Types.h"

namespace won::rendering
{
    enum class RenderPathType
    {
        Forward
    };

    enum class ViewResizePolicy
    {
        Manual,
        MatchWindow
    };

    struct Rect
    {
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
    };

    struct ViewOptions
    {
        ViewResizePolicy resize_policy = ViewResizePolicy::MatchWindow;
        bool update_camera_aspect = true;
        bool enable_frustum_culling = true;
    };

    class View
    {
    public:
        ecs::Entity camera_entity = {};
        ecs::Scene* scene = nullptr;
        RenderPathType render_path_type = RenderPathType::Forward;
        ViewOptions options = {};
        Rect viewport = {};
        Rect scissor = {};

        Vector<uint32> sorted_opaque_indices;       // front-to-back
        Vector<uint32> sorted_transparent_indices;  // back-to-front
        Vector<uint32> sorted_sprite_3d_indices;    // back-to-front
        Vector<uint32> sorted_sprite_2d_indices;    // by layer

        void Update(float dt);
        bool RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh = true) const;

    private:
        void BuildSortedIndices();
    };
}
