#pragma once
#include "Scene.h"
#include "ViewOptionEnums.h"
#include "Types.h"
#include "RHIResource.h"
#include "RHISwapchain.h"

namespace won::rendering
{
    enum class RenderPathType
    {
        Forward,
        ForwardPlus
    };

    struct Rect
    {
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
    };

    class View
    {
    public:
        struct Options
        {
            ViewResizePolicy resize_policy = ViewResizePolicy::MatchWindow;
            bool update_camera_aspect = true;
            bool enable_frustum_culling = true;
            bool enable_viewport_culling = true; // 2D sprites only
            AntiAliasingMode aa_mode = AntiAliasingMode::None;
            TonemapMode tonemap_mode = TonemapMode::Reinhard;
        };

        struct LightResources
        {
            std::unique_ptr<RHIResource> forward_index_buffer;
            std::unique_ptr<RHIResource> forward_index_upload_buffer;
            RHISubresourceHandle forward_index_srv = {};
            uint32 forward_light_count = 0;

            std::unique_ptr<RHIResource> cluster_light_count_buffer;
            RHISubresourceHandle cluster_light_count_srv = {};
            RHISubresourceHandle cluster_light_count_uav = {};
            std::unique_ptr<RHIResource> cluster_light_offset_buffer;
            RHISubresourceHandle cluster_light_offset_srv = {};
            RHISubresourceHandle cluster_light_offset_uav = {};
            std::unique_ptr<RHIResource> cluster_light_index_buffer;
            RHISubresourceHandle cluster_light_index_srv = {};
            RHISubresourceHandle cluster_light_index_uav = {};
            uint2 cluster_dims = { 0, 0 };
            uint32 depth_slice_count = 0;
        };

        struct RenderShadowSlice
        {
            uint32 light_index = 0;
            float4x4 view_projection = math::IDENTITY_MATRIX;
            int4 shadow_map_atlas_rect = { -1, -1, 0, 0 };

            bool HasShadowMapAtlasRect() const { return shadow_map_atlas_rect.z > 0 && shadow_map_atlas_rect.w > 0; }
        };

        struct ShadowResources
        {
            Vector<ShaderShadowCascade> shader_shadow_cascades;
            Vector<RenderShadowSlice> render_shadow_slices;
            Vector<uint32> light_shadow_slices;
            uint2 shadow_map_atlas_size = { 0, 0 };

            std::unique_ptr<RHIResource> atlas;
            RHISubresourceHandle atlas_dsv = {};
            RHISubresourceHandle atlas_srv = {};

            std::unique_ptr<RHIResource> cascade_buffer;
            RHISubresourceHandle cascade_srv = {};
            std::unique_ptr<RHIResource> light_slice_buffer;
            RHISubresourceHandle light_slice_srv = {};
        };

        struct InstanceResources
        {
            std::unique_ptr<RHIResource> sort_buffer;
            RHISubresourceHandle sort_srv = {};

            std::array<std::unique_ptr<RHIResource>, max_frames_in_flight> sort_upload_buffers;
        };

        struct DDGIDebugResources
        {
            std::unique_ptr<RHIResource> probe_data_readback_buffer;
            bool probe_data_readback_valid = false;
        };

        struct RenderTargets
        {
            std::unique_ptr<RHIResource> color[2];
            RHISubresourceHandle color_rtv[2] = {};
            RHISubresourceHandle color_srv[2] = {};
            RHISubresourceHandle color_uav[2] = {};

            std::unique_ptr<RHIResource> depth;
            RHISubresourceHandle depth_dsv = {};
            RHISubresourceHandle depth_srv = {};

            uint32 width = 0;
            uint32 height = 0;
        };

        struct ViewConstants
        {
            std::unique_ptr<RHIResource> buffer;
            RHISubresourceHandle cbv = {};
        };

        struct ExposureResources
        {
            std::unique_ptr<RHIResource> luminance_partial_buffer;
            RHISubresourceHandle luminance_partial_buffer_uav = {};
            RHISubresourceHandle luminance_partial_buffer_srv = {};
            std::unique_ptr<RHIResource> luminance_buffer;
            RHISubresourceHandle luminance_buffer_uav = {};
            std::unique_ptr<RHIResource> luminance_readback_buffer;
        };

        ecs::Entity camera_entity = {};
        uint32 viewer_index = 0;
        bool manual_camera = false;
        ecs::Scene* scene = nullptr;
        RenderPathType render_path_type = RenderPathType::ForwardPlus; // pipeline-level selection, not a lightweight ViewOption
        ViewMode view_mode = ViewMode::Lit;
        uint32 show_flags = Show_Default;
        bool freeze_culling = false;
        Options options = {};
        RenderTargets render_targets = {};
        ViewConstants view_constants = {};
        ExposureResources exposure_resources = {};
        LightResources light_resources = {};
        ShadowResources shadow_resources = {};
        InstanceResources instance_resources = {};
        DDGIDebugResources ddgi_debug_resources = {};
        Rect viewport = {};
        Rect scissor = {};
        uint32 ui_layer_mask = 0xFFFFFFFF;

        Vector<uint32> sorted_opaque_indices;       // batch-key order
        Vector<uint32> sorted_transparent_indices;  // back-to-front
        Vector<uint32> sorted_sprite_3d_indices;    // back-to-front
        Vector<uint32> sorted_sprite_2d_indices;    // by layer

        void UpdateUIInteraction();
        void BuildSortedIndices();
        ecs::Entity ResolveCamera() const;
        bool RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh = true, uint32 layer_mask = 0xFFFFFFFF) const;
        bool ScreenToRay(float2 screen_position, math::Ray& out_ray) const;

    private:
        ecs::Entity HitTestUI(float2 pointer) const;
        bool HasPointerFocus() const;

        ecs::Entity ui_hovered = ecs::INVALID_ENTITY;
        ecs::Entity ui_press_target = ecs::INVALID_ENTITY;
        math::Frustum frozen_frustum = {};
        bool frozen_frustum_valid = false;
    };
}
