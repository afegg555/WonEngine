#ifndef WON_COMMON
#define WON_COMMON

#include "ShaderInterop_Renderer.h"

inline int DescriptorIndex(in int descriptor_index)
{
    return descriptor_index;
}

#define DEFAULT_ROOTSIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "RootConstants(num32BitConstants = 4, b999), " \
    "DescriptorTable(SRV(t0, space = 200, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 201, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 202, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 203, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 204, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 205, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 206, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE))"

#endif // WON_COMMON
