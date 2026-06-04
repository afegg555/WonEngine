#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "ShaderCompiler.h"
#include "ShaderManifest.h"
#include "RHIShader.h"
#include "RHIDevice.h"

namespace won::resource
{
    enum class RenderPassType : uint8
    {
        SkyPass,
        ShadowPass,
        DepthPrepass,
        MainPass,
        Sprite2DPass,
        Sprite3DPass,
        Count
    };

    enum class Sprite3DPassMode : uint8
    {
        Sprite,
        Text
    };

    enum class Sprite2DPassMode : uint8
    {
        Sprite,
        Text
    };

    struct GraphicsPipelineHash
    {
        struct Bits
        {
            uint64 render_pass_type : 5;
            uint64 pass_mode : 4; // additional bits for render pass
            uint64 topology : 3;
            uint64 depth_compare : 4;
            uint64 cull_mode : 2;
            uint64 fill_mode : 1;
            uint64 shader_type : 4;
            uint64 reserved : 41;
        };

        union Storage
        {
            uint64 value;
            Bits bits;

            Storage()
                : value(0)
            {
            }
        } storage;

        GraphicsPipelineHash() = default;

        bool IsValid() const
        {
            return storage.value != 0;
        }

        bool operator==(const GraphicsPipelineHash& other) const
        {
            return storage.value == other.storage.value;
        }
    };

    struct ComputePipelineHash
    {
        struct Bits
        {
            uint64 compute_shader : 12;
            uint64 reserved : 52;
        };

        union Storage
        {
            uint64 value;
            Bits bits;

            Storage()
                : value(0)
            {
            }
        } storage;

        ComputePipelineHash() = default;

        bool IsValid() const
        {
            return storage.value != 0;
        }

        bool operator==(const ComputePipelineHash& other) const
        {
            return storage.value == other.storage.value;
        }
    };
    static_assert(sizeof(GraphicsPipelineHash) == sizeof(uint64), "GraphicsPipelineHash must be 8 bytes");
    static_assert(sizeof(ComputePipelineHash) == sizeof(uint64), "ComputePipelineHash must be 8 bytes");

    // TODO: determine the responsibility for pipeline management;
    // graphics / compute shader pipeline management.
    class ShaderLibrary
    {
    public:
        explicit ShaderLibrary(const ShaderCompilerOptions& options = {});
        bool LoadManifest(const ShaderManifest& manifest);
        bool BuildAllGraphicsPipelines(const std::shared_ptr<rendering::RHIDevice>& device, rendering::RHIFormat rtv_format, rendering::RHIFormat dsv_format, uint32 sample_count);
        void SetShader(ShaderId shader_id, const std::shared_ptr<rendering::RHIShader>& shader);
        
        std::shared_ptr<rendering::RHIShader> GetShader(ShaderId shader_id) const;
        std::shared_ptr<rendering::RHIPipeline> GetPipeline(GraphicsPipelineHash pipeline_hash) const;
        std::shared_ptr<rendering::RHIPipeline> GetPipeline(ComputePipelineHash pipeline_hash) const;
        void ClearPipelines();
        void ClearShaders();
        void ClearAll();
        Size GetShaderCount() const;

    private:
        ShaderCompilerOptions compiler_options = {};
        std::shared_ptr<ShaderCompiler> shader_compiler = {};
        std::array<std::shared_ptr<rendering::RHIShader>, static_cast<Size>(ShaderId::Count)> shaders;
        UnorderedMap<uint64, std::shared_ptr<rendering::RHIPipeline>> graphics_pipeline_cache;
        UnorderedMap<uint64, std::shared_ptr<rendering::RHIPipeline>> compute_pipeline_cache;
    };

    ShaderLibrary& GetShaderLibrary(); // this function is not exposed externally
}
