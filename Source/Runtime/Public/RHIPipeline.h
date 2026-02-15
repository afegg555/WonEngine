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
        RHICullMode cull_mode = RHICullMode::Back;
        bool front_ccw = false;
        bool depth_clip_enable = true;
    };

    struct RHIDepthStencilDesc
    {
        bool depth_test = true;
        bool depth_write = true;
        RHICompareOp depth_compare = RHICompareOp::LessEqual;
    };

    struct RHIBlendDesc
    {
        bool enable = false;
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

    class WONENGINE_API RHIPipeline : public RHIObject
    {
    public:
        ~RHIPipeline() override = default;

        virtual bool IsCompute() const = 0;
    };
}
