#ifndef WON_SHADERINTEROP_H
#define WON_SHADERINTEROP_H

#ifdef __cplusplus

// Application-side types:
#include "MathTypes.h"

#define CB_GETBINDSLOT(name) __CBUFFERBINDSLOT__##name##__
#define CBUFFER(name, slot) static const int CB_GETBINDSLOT(name) = slot; struct alignas(16) name
#define CONSTANTBUFFER(name, type, slot)
#define PUSHCONSTANT(name, type)

#else

// Shader - side types:

#define alignas(x)

#define PASTE1(a, b) a##b
#define PASTE(a, b) PASTE1(a, b)
#define CBUFFER(name, slot) cbuffer name : register(PASTE(b, slot))
#define CONSTANTBUFFER(name, type, slot) ConstantBuffer< type > name : register(PASTE(b, slot))

#if defined(__spirv__)
#define PUSHCONSTANT(name, type) [[vk::push_constant]] type name;
#else
#define PUSHCONSTANT(name, type) ConstantBuffer<type> name : register(b999)
#endif // __spirv__

#endif // __cplusplus


// Common buffers:
// These are usable by all shaders
#define CBSLOT_IMAGE							0
#define CBSLOT_FONT								0
#define CBSLOT_RENDERER_FRAME					0
#define CBSLOT_RENDERER_CAMERA					1

// On demand buffers:
// These are bound on demand and alive until another is bound at the same slot
#define CBSLOT_RENDERER_FORWARD_LIGHTMASK		2
#define CBSLOT_RENDERER_VOLUMELIGHT				3
#define CBSLOT_RENDERER_VOXELIZER				3
#define CBSLOT_RENDERER_TRACED					2
#define CBSLOT_RENDERER_MISC					3

#endif // WON_SHADERINTEROP_H
