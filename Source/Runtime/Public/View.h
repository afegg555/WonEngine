#pragma once
#include "Scene.h"
#include "ViewOptionEnums.h"
#include "Types.h"
#include "RHIResource.h"
#include "RHISwapchain.h"
#include "RHIQueryHeap.h"
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
            bool enable_occlusion_culling = true;
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

        struct TransformResources
        {
            FrameGraphResourceRef transform_index_buffer = invalid_frame_resource;
            RHISubresourceHandle transform_index_srv = {};
        };

        struct DDGIDebugResources
        {
            std::unique_ptr<RHIResource> probe_data_readback_buffer;
            bool probe_data_readback_valid = false;
        };

        struct OcclusionResources
        {
            struct RenderableKey
            {
                ecs::Entity entity = ecs::INVALID_ENTITY;
                uint32 geometry_index = 0;

                bool operator==(const RenderableKey& other) const
                {
                    return entity == other.entity && geometry_index == other.geometry_index;
                }
            };

            struct RenderableKeyHasher
            {
                Size operator()(const RenderableKey& key) const
                {
                    return static_cast<Size>(key.entity) ^ (static_cast<Size>(key.geometry_index) << 32);
                }
            };

            struct VisibilityEntry
            {
                uint8 history = 0xffu;
                uint8 queried = 0xffu;

                bool IsOccluded() const { return history == 0 && (queried & 1u) != 0; }
                bool IsDead() const { return queried == 0; }
            };

            std::unique_ptr<RHIQueryHeap> query_heap;
            std::array<std::unique_ptr<RHIResource>, max_frames_in_flight> readback_buffers = {};
            std::array<Vector<RenderableKey>, max_frames_in_flight> issued_keys = {};
            Vector<ShaderOcclusionBox> query_boxes; // 1:1 with View::occlusion_query_indices
            std::unordered_map<RenderableKey, VisibilityEntry, RenderableKeyHasher> visibility;
            bool active = false;
        };

        struct RenderTargets
        {
            FrameGraphResourceRef scene_color = invalid_frame_resource;
            RHISubresourceHandle scene_color_rtv = {};
            RHISubresourceHandle scene_color_srv = {};
            RHISubresourceHandle scene_color_uav = {};

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
            ShaderCamera camera = {};
        };

        struct ExposureResources
        {
            std::unique_ptr<RHIResource> luminance_readback_buffer;
            float measured_luminance = -1.0f;
        };

        struct TemporalAAResources
        {
            std::unique_ptr<RHIResource> history_texture[2] = {};
            RHISubresourceHandle history_srv[2] = {};
            RHISubresourceHandle history_uav[2] = {};
            std::unique_ptr<RHIResource> depth_history_texture[2] = {};
            RHISubresourceHandle depth_history_srv[2] = {};
            RHISubresourceHandle depth_history_uav[2] = {};
            float4x4 previous_view_projection = math::IDENTITY_MATRIX;
            float3 previous_position = { 0.0f, 0.0f, 0.0f };
            float3 previous_forward = { 0.0f, 0.0f, 1.0f };
            ecs::Entity history_camera_entity = ecs::INVALID_ENTITY;
            uint32 history_index = 0; // = read_index    write_index = read_index ^ 1u
            uint32 jitter_index = 0;
            bool history_valid = false;
        };

        struct SpriteResources
        {
            Vector<ShaderSprite> sprites_3d; // 1:1 with View::sorted_sprite_3d_indices
            Vector<ShaderSprite> sprites_2d; // 1:1 with View::sorted_sprite_2d_indices
            RHISubresourceHandle sprite_3d_srv = {};
            RHISubresourceHandle sprite_2d_srv = {};
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
        ecs::CameraComponent cached_camera = {};
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
        TemporalAAResources temporal_aa_resources = {};
        SpriteResources sprite_resources = {};
        WaterResources water_resources = {};
        LightResources light_resources = {};
        ShadowResources shadow_resources = {};
        TransformResources transform_resources = {};
        DDGIDebugResources ddgi_debug_resources = {};
        OcclusionResources occlusion_resources = {};
        Rect viewport = {};
        Rect scissor = {};
        uint32 ui_layer_mask = 0xFFFFFFFF;


        // renderable indices
        Vector<uint32> sorted_shadow_caster_indices; // per shadow slice, concatenated in slice order; each range is in batch-key order
        Vector<uint32> sorted_opaque_indices;       // batch-key order, occlusion applied
        Vector<uint32> occlusion_query_indices;     // frustum survivors, occlusion NOT applied
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
