#include "DecalUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"

namespace won::ecs
{
    void DecalUpdateSystem::Update(Scene& scene, float delta_time)
    {
        Scene::RenderData& render_data = scene.GetRenderData();
        render_data.shader_decals.clear();

        auto decal_array = scene.GetComponentArray<DecalComponent>().get();
        if (!decal_array)
        {
            return;
        }
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        if (!transform_array || !material_array)
        {
            return;
        }

        for (Size i = 0; i < decal_array->GetSize(); ++i)
        {
            const DecalComponent& decal = decal_array->data[i];
            if (!decal.IsActive())
            {
                continue;
            }

            const Entity entity = decal_array->index_to_entity[i];
            if (!transform_array->HasData(entity) || !material_array->HasData(entity))
            {
                continue;
            }

            const MaterialComponent& material = material_array->GetData(entity);
            if (!material.material || material.material->slots.empty())
            {
                continue;
            }

            const TransformComponent& transform = transform_array->GetData(entity);
            const XMMATRIX world = XMLoadFloat4x4(&transform.world_transform);

            ShaderDecal shader_decal = {};
            shader_decal.Init();
            XMStoreFloat4x4(&shader_decal.inv_world, XMMatrixInverse(nullptr, world));
            shader_decal.instance_index = static_cast<uint32>(transform_array->entity_to_index[entity]);
            shader_decal.material_index = material.material->material_offset;
            render_data.shader_decals.push_back(shader_decal);
        }
    }
}
