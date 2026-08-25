#pragma once
#include "RHIResource.h"
#include "RHIShader.h"
#include "Types.h"

namespace won::rendering
{
    enum class RHIPrimitiveTopology
    {
        TriangleList,
        TriangleStrip,
        LineList,
        LineStrip,
        PointList
    };

    enum class RHICullMode
    {
        None,
        Front,
        Back
    };

    enum class RHIFillMode
    {
        Solid,
        Wireframe
    };

    enum class RHICompareOp
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always
    };

    struct RHIInputElement
    {
        String semantic_name;
        uint32 semantic_index = 0;
        RHIFormat format = RHIFormat::Unknown;
        uint32 input_slot = 0;
        uint32 byte_offset = 0;
        bool per_instance = false; // for instancing
        uint32 instance_step_rate = 0;
    };

    struct RHIRasterDesc
    {
        RHIFillMode fill_mode = RHIFillMode::Solid;
        RHICullMode cull_mode = RHICullMode::None;
        bool front_ccw = false;
        bool depth_clip_enable = true;
        int32 depth_bias = 0;
        float depth_bias_clamp = 0.0f;
        float slope_scaled_depth_bias = 0.0f;
        bool multisample_enable = false;
        bool antialiased_line_enable = false;
        bool conservative_raster = false;
    };

    struct RHIDepthStencilDesc
    {
        bool depth_test = true;
        bool depth_write = false;
        RHICompareOp depth_compare = RHICompareOp::GreaterEqual;
    };

    enum class RHIBlendMode
    {
        Alpha,        // src_alpha / inv_src_alpha
        Additive,     // src_alpha / one
        Premultiplied, // one / inv_src_alpha (source already multiplied by its alpha)
    };

    struct RHIBlendDesc
    {
        bool enable = false;
        RHIBlendMode mode = RHIBlendMode::Alpha;
    };

    struct RHIGraphicsPipelineDesc
    {
        const RHIShader* vertex_shader = nullptr;
        const RHIShader* pixel_shader = nullptr;
        Vector<RHIInputElement> input_layout;
        Vector<RHIFormat> render_target_formats = { RHIFormat::R8G8B8A8Unorm };
        RHIFormat depth_stencil_format = RHIFormat::D32Float;
        uint32 sample_count = 1;
        RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
        RHIRasterDesc raster = {};
        RHIDepthStencilDesc depth_stencil = {};
        RHIBlendDesc blend = {};
    };

    struct RHIComputePipelineDesc
    {
        const RHIShader* compute_shader = nullptr;
    };

    static constexpr uint32 descriptor_binder_cbv_count = 14;
    static constexpr uint32 descriptor_binder_srv_count = 16;
    static constexpr uint32 descriptor_binder_uav_count = 16;
    static constexpr uint32 descriptor_binder_sampler_count = 8;

    class RHIPipeline : public RHIObject
    {
    public:
        ~RHIPipeline() override = default;

        virtual bool IsCompute() const = 0;
    };
}
