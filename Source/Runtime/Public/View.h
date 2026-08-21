#pragma once
#include "Scene.h"
#include "ViewOptionEnums.h"
#include "Types.h"
#include "RHIResource.h"
#include "RHISwapchain.h"
#include "FrameGraph.h"

namespace won::rendering
{
    using FrameGraphResourceRef = FrameResourceId;

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
            float shadow_resolution_scale = 1.0f;
        };

        struct LightResources
        {
            FrameGraphResourceRef forward_index_buffer = invalid_frame_resource;
            RHISubresourceHandle forward_index_srv = {};
            uint32 forward_light_count = 0;
            Vector<uint32> visible_forward_lights;

            FrameGraphResourceRef cluster_light_count_buffer = invalid_frame_resource;
            RHISubresourceHandle cluster_light_count_srv = {};
            RHISubresourceHandle cluster_light_count_uav = {};
            FrameGraphResourceRef cluster_light_offset_buffer = invalid_frame_resource;
            RHISubresourceHandle cluster_light_offset_srv = {};
            RHISubresourceHandle cluster_light_offset_uav = {};
            FrameGraphResourceRef cluster_light_index_buffer = invalid_frame_resource;
            RHISubresourceHandle cluster_light_index_srv = {};
            RHISubresourceHandle cluster_light_index_uav = {};
            uint2 cluster_dims = { 0, 0 };
            uint32 depth_slice_count = 0;
        };

        struct RenderShadowSlice
        {
            uint32 light_index = 0;
            float4x4 view_projection = math::IDENTITY_MATRIX;
            math::Frustum casting_frustum = {};
            int4 shadow_map_atlas_rect = { -1, -1, 0, 0 };

            bool HasShadowMapAtlasRect() const { return shadow_map_atlas_rect.z > 0 && shadow_map_atlas_rect.w > 0; }
        };

        struct ShadowResources
        {
			Vector<ShaderShadowCascade> shader_shadow_cascades; // for gpu
            Vector<RenderShadowSlice> render_shadow_slices; // for cpu, SetViewport, SetScissor, etc.
			Vector<uint32> light_shadow_slices; // cascade offset(16bit) and cascade count(16bit) per light
            Vector<uint2> caster_slice_ranges; // per slice {offset, count} into sorted_shadow_caster_indices
            Vector<Vector<uint32>> caster_slice_scratch; // kept across frames so the per slice culling jobs reuse their capacity
            uint2 shadow_map_atlas_size = { 0, 0 };

            FrameGraphResourceRef atlas = invalid_frame_resource;
            RHISubresourceHandle atlas_dsv = {};
            RHISubresourceHandle atlas_srv = {};

            FrameGraphResourceRef cascade_buffer = invalid_frame_resource;
            RHISubresourceHandle cascade_srv = {};
            FrameGraphResourceRef light_slice_buffer = invalid_frame_resource;
            RHISubresourceHandle light_slice_srv = {};
        };

        struct InstanceResources
        {
            FrameGraphResourceRef sort_buffer = invalid_frame_resource;
            RHISubresourceHandle sort_srv = {};
        };

        struct DDGIDebugResources
        {
            std::unique_ptr<RHIResource> probe_data_readback_buffer;
            bool probe_data_readback_valid = false;
        };

        struct RenderTargets
        {
            FrameGraphResourceRef color[2] = { invalid_frame_resource, invalid_frame_resource };
            RHISubresourceHandle color_rtv[2] = {};
            RHISubresourceHandle color_srv[2] = {};
            RHISubresourceHandle color_uav[2] = {};

            FrameGraphResourceRef depth = invalid_frame_resource;
            RHISubresourceHandle depth_dsv = {};
            RHISubresourceHandle depth_readonly_dsv = {};
            RHISubresourceHandle depth_srv = {};

            FrameGraphResourceRef scene_color_snapshot = invalid_frame_resource;
            RHISubresourceHandle scene_color_snapshot_srv = {};

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
            std::unique_ptr<RHIResource> luminance_readback_buffer;
        };

        struct WaterResources
        {
            struct TileRange
            {
                uint32 first_tile = 0;
                uint32 tile_count = 0;
            };

			Vector<ShaderWaterTile> tiles; // flattened list of all water tiles for all zones
			Vector<TileRange> zone_tile_ranges; // TileRange per water zone
            FrameGraphResourceRef tile_buffer = invalid_frame_resource;
            RHISubresourceHandle tile_srv = {};
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
        WaterResources water_resources = {};
        LightResources light_resources = {};
        ShadowResources shadow_resources = {};
        InstanceResources instance_resources = {};
        DDGIDebugResources ddgi_debug_resources = {};
        Rect viewport = {};
        Rect scissor = {};
        uint32 ui_layer_mask = 0xFFFFFFFF;


        // renderable indices
        Vector<uint32> sorted_shadow_caster_indices; // per shadow slice, concatenated in slice order; each range is in batch-key order
        Vector<uint32> sorted_opaque_indices;       // batch-key order
        Vector<uint32> sorted_transparent_indices;  // back-to-front
        Vector<uint32> sorted_sprite_3d_indices;    // back-to-front
        Vector<uint32> sorted_sprite_2d_indices;    // by layer

        void Update(float delta_time, uint64 update_index, bool simulation_paused);
        bool RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh = true, uint32 layer_mask = 0xFFFFFFFF) const;
        bool ScreenToRay(float2 screen_position, math::Ray& out_ray) const;

    private:
        void UpdateUIInteraction();
        void BuildShadowSlices();
        void BuildSortedIndices();
        void BuildForwardLightList();

        void BuildWaterTiles();
        ecs::Entity ResolveCamera() const;

        ecs::Entity HitTestUI(float2 pointer) const;
        bool HasPointerFocus() const;

        ecs::Entity ui_hovered = ecs::INVALID_ENTITY;
        ecs::Entity ui_press_target = ecs::INVALID_ENTITY;
        math::Frustum frozen_frustum = {};
        bool frozen_frustum_valid = false;
    };
}
