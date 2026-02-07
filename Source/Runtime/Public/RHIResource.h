#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"
#include "RHISampler.h"

namespace won::rendering
{
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
        Immutable,
        Dynamic,
        Staging
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
        CopySource = 1u << 7,
        CopyDest = 1u << 8
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

    enum class RHIResourceState
    {
        Undefined,
        CopySource,
        CopyDest,
        ShaderRead,
        ShaderWrite,
        RenderTarget,
        DepthWrite,
        Present
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
        RHIFormat format = RHIFormat::Unknown;
        RHIResourceUsage usage = RHIResourceUsage::Default;
        RHIBindFlags bind_flags = RHIBindFlags::None;
        RHIMiscFlags misc_flags = RHIMiscFlags::None;
    };

    struct RHIResourceDesc
    {
        RHIResourceType type = RHIResourceType::Unknown;
        RHIBufferDesc buffer_desc = {};
        RHITextureDesc texture_desc = {};
    };

    class WONENGINE_API RHIResource : public RHIObject
    {
    public:
        virtual ~RHIResource() = default;

        virtual const RHIResourceDesc& GetDesc() const = 0;
    };
}
