#pragma once
#include "MathTypes.h"
#include "Material.h"
#include "Types.h"

#include <cassert>
#include <memory>

namespace won::ecs
{
    struct MaterialComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
        };
        uint32 flags = Dirty;

        // keep component lightweight: reference a shared material, which is owned by the resource layer
        std::shared_ptr<resource::Material> material;
        String material_asset_path;
        uint32 material_offset = 0;

        void SetMaterial(const std::shared_ptr<resource::Material>& value)
        {
            if (material == value)
            {
                return;
            }

            material = value;
            SetDirty();
        }

        void SetMaterialAssetPath(const String& value)
        {
            material_asset_path = value;
        }

        void ForkMaterial()
        {
            if (!material)
            {
                return;
            }

            // Fork is about exclusive ownership, not the asset path: a cache-backed material can be
            // re-shared via the path cache, and a pathless material may still be shared by others.
            const bool cache_backed = !material_asset_path.empty();
            if (!cache_backed && material.use_count() <= 1)
            {
                return; // already this component's private instance
            }

            material = std::make_shared<resource::Material>(*material);
            material_offset = 0;
            material_asset_path.clear();
            SetDirty();
        }

        resource::MaterialSlot& GetMaterialSlot(uint32 slot_index = 0u)
        {
            assert(material && slot_index < material->slots.size());

            return material->slots[slot_index];
        }

        resource::MaterialSlot& AddMaterialSlot()
        {
            if (!material)
            {
                material = std::make_shared<resource::Material>();
            }

            material->slots.push_back({});
            SetDirty();
            return material->slots.back();
        }

        Size GetMaterialSlotCount() const
        {
            return material ? material->slots.size() : 0;
        }

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
    };
}
