#pragma once
#include "Scene.h"
#include "ViewOptionEnums.h"
#include "Types.h"

namespace won::rendering
{
    enum class RenderPathType
    {
        Forward
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
        bool enable_viewport_culling = true; // 2D sprites only
        AntiAliasingMode aa_mode = AntiAliasingMode::None;
        TonemapMode tonemap_mode = TonemapMode::Reinhard;
    };

    class View
    {
    public:
        ecs::Entity camera_entity = {};
        ecs::Scene* scene = nullptr;
        RenderPathType render_path_type = RenderPathType::Forward; // pipeline-level selection, not a lightweight ViewOption
        ViewOptions options = {};
        Rect viewport = {};
        Rect scissor = {};
        uint32 ui_layer_mask = 0xFFFFFFFF;

        Vector<uint32> sorted_opaque_indices;       // batch-key order
        Vector<uint32> sorted_transparent_indices;  // back-to-front
        Vector<uint32> sorted_sprite_3d_indices;    // back-to-front
        Vector<uint32> sorted_sprite_2d_indices;    // by layer

        void UpdateUIInteraction();
        void BuildSortedIndices();
        bool RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh = true, uint32 layer_mask = 0xFFFFFFFF) const;

    private:
        ecs::Entity HitTestUI(float2 pointer) const;
        bool HasPointerFocus() const;

        ecs::Entity ui_hovered = ecs::INVALID_ENTITY;
        ecs::Entity ui_press_target = ecs::INVALID_ENTITY;
    };
}
