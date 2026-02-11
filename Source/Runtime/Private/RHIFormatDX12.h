#pragma once
#include "RHIResource.h"
#include "DirectX-Headers/dxgiformat.h"

namespace won::rendering
{
    inline DXGI_FORMAT ToDXGIFormat(RHIFormat format)
    {
        switch (format)
        {
        case RHIFormat::R32G32B32A32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R32G32B32A32Uint: return DXGI_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::R32G32B32A32Sint: return DXGI_FORMAT_R32G32B32A32_SINT;
        case RHIFormat::R32G32B32Float: return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::R32G32B32Uint: return DXGI_FORMAT_R32G32B32_UINT;
        case RHIFormat::R32G32B32Sint: return DXGI_FORMAT_R32G32B32_SINT;
        case RHIFormat::R16G16B16A16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R16G16B16A16Unorm: return DXGI_FORMAT_R16G16B16A16_UNORM;
        case RHIFormat::R16G16B16A16Uint: return DXGI_FORMAT_R16G16B16A16_UINT;
        case RHIFormat::R16G16B16A16Snorm: return DXGI_FORMAT_R16G16B16A16_SNORM;
        case RHIFormat::R16G16B16A16Sint: return DXGI_FORMAT_R16G16B16A16_SINT;
        case RHIFormat::R32G32Float: return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::R32G32Uint: return DXGI_FORMAT_R32G32_UINT;
        case RHIFormat::R32G32Sint: return DXGI_FORMAT_R32G32_SINT;
        case RHIFormat::D32FloatS8X24Uint: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        case RHIFormat::R10G10B10A2Unorm: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case RHIFormat::R10G10B10A2Uint: return DXGI_FORMAT_R10G10B10A2_UINT;
        case RHIFormat::R11G11B10Float: return DXGI_FORMAT_R11G11B10_FLOAT;
        case RHIFormat::R8G8B8A8Unorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::R8G8B8A8UnormSrgb: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::R8G8B8A8Uint: return DXGI_FORMAT_R8G8B8A8_UINT;
        case RHIFormat::R8G8B8A8Snorm: return DXGI_FORMAT_R8G8B8A8_SNORM;
        case RHIFormat::R8G8B8A8Sint: return DXGI_FORMAT_R8G8B8A8_SINT;
        case RHIFormat::B8G8R8A8Unorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::B8G8R8A8UnormSrgb: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R16G16Float: return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::R16G16Unorm: return DXGI_FORMAT_R16G16_UNORM;
        case RHIFormat::R16G16Uint: return DXGI_FORMAT_R16G16_UINT;
        case RHIFormat::R16G16Snorm: return DXGI_FORMAT_R16G16_SNORM;
        case RHIFormat::R16G16Sint: return DXGI_FORMAT_R16G16_SINT;
        case RHIFormat::D32Float: return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::R32Float: return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::R32Uint: return DXGI_FORMAT_R32_UINT;
        case RHIFormat::R32Sint: return DXGI_FORMAT_R32_SINT;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::R9G9B9E5Sharedexp: return DXGI_FORMAT_R9G9B9E5_SHAREDEXP;
        case RHIFormat::R8G8Unorm: return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::R8G8Uint: return DXGI_FORMAT_R8G8_UINT;
        case RHIFormat::R8G8Snorm: return DXGI_FORMAT_R8G8_SNORM;
        case RHIFormat::R8G8Sint: return DXGI_FORMAT_R8G8_SINT;
        case RHIFormat::R16Float: return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::D16Unorm: return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::R16Unorm: return DXGI_FORMAT_R16_UNORM;
        case RHIFormat::R16Uint: return DXGI_FORMAT_R16_UINT;
        case RHIFormat::R16Snorm: return DXGI_FORMAT_R16_SNORM;
        case RHIFormat::R16Sint: return DXGI_FORMAT_R16_SINT;
        case RHIFormat::R8Unorm: return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::R8Uint: return DXGI_FORMAT_R8_UINT;
        case RHIFormat::R8Snorm: return DXGI_FORMAT_R8_SNORM;
        case RHIFormat::R8Sint: return DXGI_FORMAT_R8_SINT;
        case RHIFormat::BC1Unorm: return DXGI_FORMAT_BC1_UNORM;
        case RHIFormat::BC1UnormSrgb: return DXGI_FORMAT_BC1_UNORM_SRGB;
        case RHIFormat::BC2Unorm: return DXGI_FORMAT_BC2_UNORM;
        case RHIFormat::BC2UnormSrgb: return DXGI_FORMAT_BC2_UNORM_SRGB;
        case RHIFormat::BC3Unorm: return DXGI_FORMAT_BC3_UNORM;
        case RHIFormat::BC3UnormSrgb: return DXGI_FORMAT_BC3_UNORM_SRGB;
        case RHIFormat::BC4Unorm: return DXGI_FORMAT_BC4_UNORM;
        case RHIFormat::BC4Snorm: return DXGI_FORMAT_BC4_SNORM;
        case RHIFormat::BC5Unorm: return DXGI_FORMAT_BC5_UNORM;
        case RHIFormat::BC5Snorm: return DXGI_FORMAT_BC5_SNORM;
        case RHIFormat::BC6HUf16: return DXGI_FORMAT_BC6H_UF16;
        case RHIFormat::BC6HSf16: return DXGI_FORMAT_BC6H_SF16;
        case RHIFormat::BC7Unorm: return DXGI_FORMAT_BC7_UNORM;
        case RHIFormat::BC7UnormSrgb: return DXGI_FORMAT_BC7_UNORM_SRGB;
        default: return DXGI_FORMAT_UNKNOWN;
        }
    }
}
