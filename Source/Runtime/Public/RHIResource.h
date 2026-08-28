#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"
#include "RHISampler.h"

namespace won::rendering
{
    inline constexpr float OPTIMIZED_FAST_CLEAR_COLOR[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    inline constexpr float OPTIMIZED_FAST_CLEAR_DEPTH = 0.0f;
    inline constexpr uint8 OPTIMIZED_FAST_CLEAR_STENCIL = 0;

    enum class RHIResourceType
    {
        Unknown,
        Buffer,
        Texture2D,
        Texture3D,
        TextureCube
    };

    enum class RHIResourceUsage
    {
        Default,
        Upload,
        Readback
    };

    enum class RHIBindFlags : uint32
    {
        None = 0,
        VertexBuffer = 1u << 0,
        IndexBuffer = 1u << 1,
        ConstantBuffer = 1u << 2,
        ShaderResource = 1u << 3,
        UnorderedAccess = 1u << 4,
        RenderTarget = 1u << 5,
        DepthStencil = 1u << 6,
    };

    enum class RHIMiscFlags : uint32
    {
        None = 0,
    };

    inline RHIBindFlags operator|(RHIBindFlags lhs, RHIBindFlags rhs)
    {
        return static_cast<RHIBindFlags>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
    }

    inline RHIBindFlags operator&(RHIBindFlags lhs, RHIBindFlags rhs)
    {
        return static_cast<RHIBindFlags>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
    }

    inline bool HasBindFlag(RHIBindFlags flags, RHIBindFlags flag)
    {
        return (static_cast<uint32>(flags) & static_cast<uint32>(flag)) != 0;
    }

    enum class RHIFormat
    {
        Unknown,

        R32G32B32A32Float,
        R32G32B32A32Uint,
        R32G32B32A32Sint,

        R32G32B32Float,
        R32G32B32Uint,
        R32G32B32Sint,

        R16G16B16A16Float,
        R16G16B16A16Unorm,
        R16G16B16A16Uint,
        R16G16B16A16Snorm,
        R16G16B16A16Sint,

        R32G32Float,
        R32G32Uint,
        R32G32Sint,
        D32FloatS8X24Uint,

        R10G10B10A2Unorm,
        R10G10B10A2Uint,
        R11G11B10Float,
        R8G8B8A8Unorm,
        R8G8B8A8UnormSrgb,
        R8G8B8A8Uint,
        R8G8B8A8Snorm,
        R8G8B8A8Sint,
        B8G8R8A8Unorm,
        B8G8R8A8UnormSrgb,
        R16G16Float,
        R16G16Unorm,
        R16G16Uint,
        R16G16Snorm,
        R16G16Sint,
        D32Float,
        R32Float,
        R32Uint,
        R32Sint,
        D24UnormS8Uint,
        R9G9B9E5Sharedexp,

        R8G8Unorm,
        R8G8Uint,
        R8G8Snorm,
        R8G8Sint,
        R16Float,
        D16Unorm,
        R16Unorm,
        R16Uint,
        R16Snorm,
        R16Sint,

        R8Unorm,
        R8Uint,
        R8Snorm,
        R8Sint,

        BC1Unorm,
        BC1UnormSrgb,
        BC2Unorm,
        BC2UnormSrgb,
        BC3Unorm,
        BC3UnormSrgb,
        BC4Unorm,
        BC4Snorm,
        BC5Unorm,
        BC5Snorm,
        BC6HUf16,
        BC6HSf16,
        BC7Unorm,
        BC7UnormSrgb
    };

    inline const char* GetRHIFormatName(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::Unknown: return "Unknown";
        case RHIFormat::R32G32B32A32Float: return "R32G32B32A32Float";
        case RHIFormat::R32G32B32A32Uint: return "R32G32B32A32Uint";
        case RHIFormat::R32G32B32A32Sint: return "R32G32B32A32Sint";
        case RHIFormat::R32G32B32Float: return "R32G32B32Float";
        case RHIFormat::R32G32B32Uint: return "R32G32B32Uint";
        case RHIFormat::R32G32B32Sint: return "R32G32B32Sint";
        case RHIFormat::R16G16B16A16Float: return "R16G16B16A16Float";
        case RHIFormat::R16G16B16A16Unorm: return "R16G16B16A16Unorm";
        case RHIFormat::R16G16B16A16Uint: return "R16G16B16A16Uint";
        case RHIFormat::R16G16B16A16Snorm: return "R16G16B16A16Snorm";
        case RHIFormat::R16G16B16A16Sint: return "R16G16B16A16Sint";
        case RHIFormat::R32G32Float: return "R32G32Float";
        case RHIFormat::R32G32Uint: return "R32G32Uint";
        case RHIFormat::R32G32Sint: return "R32G32Sint";
        case RHIFormat::D32FloatS8X24Uint: return "D32FloatS8X24Uint";
        case RHIFormat::R10G10B10A2Unorm: return "R10G10B10A2Unorm";
        case RHIFormat::R10G10B10A2Uint: return "R10G10B10A2Uint";
        case RHIFormat::R11G11B10Float: return "R11G11B10Float";
        case RHIFormat::R8G8B8A8Unorm: return "R8G8B8A8Unorm";
        case RHIFormat::R8G8B8A8UnormSrgb: return "R8G8B8A8UnormSrgb";
        case RHIFormat::R8G8B8A8Uint: return "R8G8B8A8Uint";
        case RHIFormat::R8G8B8A8Snorm: return "R8G8B8A8Snorm";
        case RHIFormat::R8G8B8A8Sint: return "R8G8B8A8Sint";
        case RHIFormat::B8G8R8A8Unorm: return "B8G8R8A8Unorm";
        case RHIFormat::B8G8R8A8UnormSrgb: return "B8G8R8A8UnormSrgb";
        case RHIFormat::R16G16Float: return "R16G16Float";
        case RHIFormat::R16G16Unorm: return "R16G16Unorm";
        case RHIFormat::R16G16Uint: return "R16G16Uint";
        case RHIFormat::R16G16Snorm: return "R16G16Snorm";
        case RHIFormat::R16G16Sint: return "R16G16Sint";
        case RHIFormat::D32Float: return "D32Float";
        case RHIFormat::R32Float: return "R32Float";
        case RHIFormat::R32Uint: return "R32Uint";
        case RHIFormat::R32Sint: return "R32Sint";
        case RHIFormat::D24UnormS8Uint: return "D24UnormS8Uint";
        case RHIFormat::R9G9B9E5Sharedexp: return "R9G9B9E5Sharedexp";
        case RHIFormat::R8G8Unorm: return "R8G8Unorm";
        case RHIFormat::R8G8Uint: return "R8G8Uint";
        case RHIFormat::R8G8Snorm: return "R8G8Snorm";
        case RHIFormat::R8G8Sint: return "R8G8Sint";
        case RHIFormat::R16Float: return "R16Float";
        case RHIFormat::D16Unorm: return "D16Unorm";
        case RHIFormat::R16Unorm: return "R16Unorm";
        case RHIFormat::R16Uint: return "R16Uint";
        case RHIFormat::R16Snorm: return "R16Snorm";
        case RHIFormat::R16Sint: return "R16Sint";
        case RHIFormat::R8Unorm: return "R8Unorm";
        case RHIFormat::R8Uint: return "R8Uint";
        case RHIFormat::R8Snorm: return "R8Snorm";
        case RHIFormat::R8Sint: return "R8Sint";
        case RHIFormat::BC1Unorm: return "BC1Unorm";
        case RHIFormat::BC1UnormSrgb: return "BC1UnormSrgb";
        case RHIFormat::BC2Unorm: return "BC2Unorm";
        case RHIFormat::BC2UnormSrgb: return "BC2UnormSrgb";
        case RHIFormat::BC3Unorm: return "BC3Unorm";
        case RHIFormat::BC3UnormSrgb: return "BC3UnormSrgb";
        case RHIFormat::BC4Unorm: return "BC4Unorm";
        case RHIFormat::BC4Snorm: return "BC4Snorm";
        case RHIFormat::BC5Unorm: return "BC5Unorm";
        case RHIFormat::BC5Snorm: return "BC5Snorm";
        case RHIFormat::BC6HUf16: return "BC6HUf16";
        case RHIFormat::BC6HSf16: return "BC6HSf16";
        case RHIFormat::BC7Unorm: return "BC7Unorm";
        case RHIFormat::BC7UnormSrgb: return "BC7UnormSrgb";
        }
        return "Unknown";
    }

    enum class RHIResourceState
    {
        Undefined,
        CopySource,
        CopyDest,
        ShaderRead,
        ConstantBuffer,
        ShaderWrite,
        RenderTarget,
        DepthWrite,
        DepthRead,
        Present
    };

    enum class RHISubresourceType
    {
        Unknown,
        ConstantBuffer,
        ShaderResource,
        UnorderedAccess,
        RenderTarget,
        DepthStencil,
        VertexBuffer,
        IndexBuffer
    };

    struct RHISubresourceDesc
    {
        RHISubresourceType type = RHISubresourceType::Unknown;
        RHIFormat format = RHIFormat::Unknown;
        uint32 first_slice = 0;
        uint32 slice_count = 1;
        uint32 first_mip = 0;
        uint32 mip_count = 1;
        Size buffer_offset = 0;
        Size buffer_size = 0;
        Size buffer_stride = 0;
        bool read_only = false;
    };

    inline bool operator==(const RHISubresourceDesc& lhs, const RHISubresourceDesc& rhs)
    {
        return lhs.type == rhs.type
            && lhs.format == rhs.format
            && lhs.first_slice == rhs.first_slice
            && lhs.slice_count == rhs.slice_count
            && lhs.first_mip == rhs.first_mip
            && lhs.mip_count == rhs.mip_count
            && lhs.buffer_offset == rhs.buffer_offset
            && lhs.buffer_size == rhs.buffer_size
            && lhs.buffer_stride == rhs.buffer_stride
            && lhs.read_only == rhs.read_only;
    }

    struct RHISubresourceHandle
    {
        int descriptor_index = -1;

        bool IsValid() const
        {
            return descriptor_index >= 0;
        }
    };

    struct RHIBufferDesc
    {
        Size size = 0;
        RHIResourceUsage usage = RHIResourceUsage::Default;
        RHIBindFlags bind_flags = RHIBindFlags::None;
        RHIMiscFlags misc_flags = RHIMiscFlags::None;
    };

    struct RHITextureDesc
    {
        uint32 width = 0;
        uint32 height = 0;
        uint32 depth = 1;
        uint32 mip_levels = 1;
        uint32 array_layers = 1;
        uint32 sample_count = 1;
        bool is_cube = false;
        RHIFormat format = RHIFormat::Unknown;
        RHIResourceUsage usage = RHIResourceUsage::Default;
        RHIBindFlags bind_flags = RHIBindFlags::None;
        RHIMiscFlags misc_flags = RHIMiscFlags::None;
        float clear_color[4] = { OPTIMIZED_FAST_CLEAR_COLOR[0], OPTIMIZED_FAST_CLEAR_COLOR[1], OPTIMIZED_FAST_CLEAR_COLOR[2], OPTIMIZED_FAST_CLEAR_COLOR[3] };
    };

    inline bool operator==(const RHIBufferDesc& lhs, const RHIBufferDesc& rhs)
    {
        return lhs.size == rhs.size
            && lhs.usage == rhs.usage
            && lhs.bind_flags == rhs.bind_flags
            && lhs.misc_flags == rhs.misc_flags;
    }

    inline bool operator==(const RHITextureDesc& lhs, const RHITextureDesc& rhs)
    {
        return lhs.width == rhs.width
            && lhs.height == rhs.height
            && lhs.depth == rhs.depth
            && lhs.mip_levels == rhs.mip_levels
            && lhs.array_layers == rhs.array_layers
            && lhs.sample_count == rhs.sample_count
            && lhs.is_cube == rhs.is_cube
            && lhs.format == rhs.format
            && lhs.usage == rhs.usage
            && lhs.bind_flags == rhs.bind_flags
            && lhs.misc_flags == rhs.misc_flags
            && lhs.clear_color[0] == rhs.clear_color[0]
            && lhs.clear_color[1] == rhs.clear_color[1]
            && lhs.clear_color[2] == rhs.clear_color[2]
            && lhs.clear_color[3] == rhs.clear_color[3];
    }

    struct RHIResourceDesc
    {
        RHIResourceType type = RHIResourceType::Unknown;
        RHIBufferDesc buffer_desc = {};
        RHITextureDesc texture_desc = {};
    };

    enum class RHIMemoryCategory
    {
        Buffer,
        Texture,
        RenderTargetOrDepthStencil,

        Count
    };

    class RHIMemoryBlock : public RHIObject
    {
    public:
        ~RHIMemoryBlock() override = default;

        virtual Size GetSize() const = 0;
    };

    class RHIResource : public RHIObject
    {
    public:
        ~RHIResource() override = default;

        virtual const RHIResourceDesc& GetDesc() const = 0;
        virtual void* GetMappedData() const = 0;
    };
}
