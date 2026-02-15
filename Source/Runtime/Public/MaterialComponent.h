#pragma once

#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct MaterialSlot
    {
        float4 base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 0.0f;
        float roughness = 1.0f;
        bool double_sided = false;
    };

    struct MaterialComponent
    {
        Vector<MaterialSlot> material_slots = { MaterialSlot{} };

        MaterialSlot& GetMaterial(uint32 slot_index = 0u)
        {
            if (slot_index >= material_slots.size())
            {
                return material_slots[0];
            }

            return material_slots[slot_index];
        }
    };
}
