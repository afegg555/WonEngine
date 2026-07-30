#pragma once
#include "RHIResource.h"
#include "RHISwapchain.h"
#include "ShaderInterop_Renderer.h"
#include "ShaderInterop_PostProcess.h"
#include "Primitives.h"
#include "Types.h"
#include "Material.h"
#include "Mesh.h"
#include "Font.h"
#include "Entity.h"

namespace won::ecs
{
    class Scene;
}

namespace won::rendering
{
    class RHIDevice;
    class RHICommandList;

    struct GPUBuffer
    {
        std::unique_ptr<RHIResource> buffer;
        RHISubresourceHandle srv = {};
        std::array<std::unique_ptr<RHIResource>, max_frames_in_flight> staging = {};
    };

    struct Renderable
    {
        enum Flags : uint32
        {
            None = 0,
            CastShadow = 1 << 0,
            Transparent = 1 << 1,
            DoubleSided = 1 << 2,
        };

        ObjectPushConstants push_constants;
        RHIResource* index_buffer = nullptr;
        float3 world_position = {};
        math::AABB aabb = {};
        uint32 index_offset = 0;
        uint32 index_count = 0;
        uint32 flags = None;
        uint32 shader_type = SHADER_MATERIAL_TYPE_PBR;
        resource::MaterialBlendMode blend_mode = resource::MaterialBlendMode::Opaque;
        uint32 layer_mask = 0xFFFFFFFF;
        resource::PrimitiveTopology primitive_topology = resource::PrimitiveTopology::TriangleList;

        bool IsTransparent() const { return (flags & Transparent) != 0; }
        bool IsCastShadow() const { return (flags & CastShadow) != 0; }
        bool IsDoubleSided() const { return (flags & DoubleSided) != 0; }
    };

    struct Sprite3DRenderable
    {
        enum Flags : uint32
        {
            None        = 0,
            Text        = 1 << 0,
            Billboard   = 1 << 1,
            Transparent = 1 << 2,
            Particle    = 1 << 3,
        };

        uint32 instance_index = 0;
        uint32 material_index = 0;
        float3 world_position = {};
        math::AABB aabb = {};
        float2 size = { 1.0f, 1.0f };
        float2 pivot = { 0.5f, 0.5f };
        float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        uint32 flags = None;
        resource::MaterialBlendMode blend_mode = resource::MaterialBlendMode::Alpha;
        uint32 layer_mask = 0xFFFFFFFF;
        // For particles: bindless descriptor of the per-frame float4 buffer holding
        // interleaved [position, color] pairs, indexed by instance_index.
        uint32 resource_index = 0;
        resource::Font* font = nullptr;

        bool IsText()        const { return (flags & Text) != 0; }
        bool IsBillboard()   const { return (flags & Billboard) != 0; }
        bool IsTransparent() const { return (flags & Transparent) != 0; }
        bool IsParticle()    const { return (flags & Particle) != 0; }
    };

    struct Sprite2DRenderable
    {
        enum Flags : uint32
        {
            None = 0,
            Text = 1 << 0,
        };

        uint32 material_index = 0;
        float2 anchor = { 0.0f, 0.0f };
        float2 position = { 0.0f, 0.0f };
        float2 size = { 1.0f, 1.0f };
        float2 pivot = { 0.5f, 0.5f };
        float2 reference_resolution = { 0.0f, 0.0f };
        float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
        int32  layer = 0;
        uint32 layer_mask = 0xFFFFFFFF;
        float  match = 0.5f;
        uint32 flags = None;
        resource::Font* font = nullptr;

        bool IsText() const { return (flags & Text) != 0; }
    };

    // this class is not exported
    struct GPUScene
    {
        Vector<ShaderLight> shader_lights;
        Vector<math::AABB> light_bounds;
        uint32 directional_count = 0;

        GPUBuffer light_buffer;

        Vector<ShaderInstance> shader_instances;
        GPUBuffer instance_buffer;

        Vector<ShaderGeometry> shader_geometries;
        GPUBuffer geometry_buffer;

        Vector<ShaderMaterial> shader_materials;
        GPUBuffer material_buffer;

        Vector<float4> shader_bone_matrices;
        GPUBuffer bone_buffer;

        Vector<Renderable> opaque_renderables;
        Vector<Renderable> transparent_renderables;
        Vector<Renderable> line_renderables;
        Vector<Renderable> point_renderables;

        Vector<Sprite2DRenderable> sprite_2d_renderables;
        Vector<Sprite3DRenderable> sprite_3d_renderables;

        Vector<float4> particle_instances;
        GPUBuffer particle_buffer;

        Vector<ShaderDecal> shader_decals;
        GPUBuffer decal_buffer;

        struct DDGIResources
        {
            std::unique_ptr<RHIResource> irradiance_texture;
            RHISubresourceHandle irradiance_texture_srv = {};
            RHISubresourceHandle irradiance_texture_uav = {};
            std::unique_ptr<RHIResource> irradiance_history_texture;
            RHISubresourceHandle irradiance_history_texture_srv = {};

            std::unique_ptr<RHIResource> visibility_texture;
            RHISubresourceHandle visibility_texture_srv = {};
            RHISubresourceHandle visibility_texture_uav = {};
            std::unique_ptr<RHIResource> visibility_history_texture;
            RHISubresourceHandle visibility_history_texture_srv = {};

            std::unique_ptr<RHIResource> probe_data_buffer;
            RHISubresourceHandle probe_data_buffer_srv = {};
            RHISubresourceHandle probe_data_buffer_uav = {};
            std::unique_ptr<RHIResource> probe_data_history_buffer;
            RHISubresourceHandle probe_data_history_buffer_srv = {};


            uint3 probe_counts = { 0, 0, 0 };
            float3 probe_spacing = { 0.0f, 0.0f, 0.0f };
            float3 volume_min = { 0.0f, 0.0f, 0.0f };
            float max_distance = 0.0f;
            uint32 probe_update_offset = 0;
            bool history_valid = false;
        };

        struct SkyLightingResources
        {
            std::unique_ptr<RHIResource> capture_texture;
            RHISubresourceHandle capture_srv = {};
            RHISubresourceHandle capture_uav = {};

            std::unique_ptr<RHIResource> irradiance_texture;
            RHISubresourceHandle irradiance_srv = {};
            RHISubresourceHandle irradiance_uav = {};

			ShaderEnvironment signature = {}; // used to detect if the resources are still valid for the current sky lighting environment
            std::unique_ptr<RHIResource> specular_texture;
            RHISubresourceHandle specular_srv = {};
            RHISubresourceHandle specular_mip_uav[sky_specular_mip_count] = {};

            uint32 bake_step = 1;
            int32 pending_specular_mip = -1;
            int32 pending_irradiance_face = -1;
            bool valid = false;
        };

        ShaderEnvironment shader_environment;
        SkyLightingResources sky_lighting = {};
        ShaderDDGIVolume shader_ddgi_volume;
        ShaderReflectionProbe shader_reflection_probe;
        ecs::Entity ddgi_volume_entity = ecs::INVALID_ENTITY;
        DDGIResources ddgi = {};

        math::AABB shadow_caster_world_bound;

        Vector<ShaderBVHNode> shader_bvh_nodes;
        GPUBuffer bvh_node_buffer;
        Vector<ShaderBVHInstance> shader_bvh_instances;
        GPUBuffer bvh_instance_buffer;

        std::array<Vector<std::unique_ptr<RHIResource>>, max_frames_in_flight> retired = {};

        uint64 synced_index = ~0ull;

        void Update(const ecs::Scene& scene, RHIDevice& device, RHICommandList& command_list, uint32 frame_slot);

    private:
        void RetireResource(std::unique_ptr<RHIResource>& resource, uint32 frame_slot);
        bool CreateSkyLightingResources(RHIDevice& device);
        void ReleaseSkyLightingResources(uint32 frame_slot);
        void ReleaseDDGIResources(uint32 frame_slot);
        bool CreateDDGIResources(RHIDevice& device, uint32 frame_slot);
    };
}
