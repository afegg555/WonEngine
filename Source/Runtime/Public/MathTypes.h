#pragma once

#include <cmath>
#include <algorithm>
#include <limits>

#if __has_include(<DirectXMath.h>)
// In this case, DirectXMath is coming from Windows SDK.
//	It is better to use this on Windows as some Windows libraries could depend on the same
//	DirectXMath headers
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
#else
// In this case, DirectXMath is coming from supplied source code
//	On platforms that don't have Windows SDK, the source code for DirectXMath is provided
//	as part of the engine utilities
#include "DirectXMath/DirectXMath.h"
#include "DirectXMath/DirectXPackedVector.h"
#include "DirectXMath/DirectXCollision.h"
#endif

using namespace DirectX;
using namespace DirectX::PackedVector;

using float3x3 = XMFLOAT3X3;
using float4x4 = XMFLOAT4X4;
using float2 = XMFLOAT2;
using float3 = XMFLOAT3;
using float4 = XMFLOAT4;
using uint = uint32_t;
using uint2 = XMUINT2;
using uint3 = XMUINT3;
using uint4 = XMUINT4;
using int2 = XMINT2;
using int3 = XMINT3;
using int4 = XMINT4;