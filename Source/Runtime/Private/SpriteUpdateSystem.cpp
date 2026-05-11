#include "SpriteUpdateSystem.h"

#include "MaterialComponent.h"
#include "Scene.h"
#include "Sprite3DComponent.h"
#include "TransformComponent.h"

#include <iterator>

namespace won::ecs
{
    void SpriteUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct SpriteBucket
        {
            Vector<Scene::RenderData::Sprite3DRenderable> sprite_3d_renderables;
        };

        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto sprite_3d_array = scene.GetComponentArray<Sprite3DComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        render_data.sprite_3d_renderables.clear();
        if (!sprite_3d_array || !transform_array || !material_array)
        {
            return;
        }

        Vector<SpriteBucket> sprite_buckets(jobsystem::GetThreadCount() + 1);
        jobsystem::Dispatch(sub_ctx, (uint32_t)sprite_3d_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            SpriteBucket& bucket = sprite_buckets[args.worker_index];

            const Entity entity = sprite_3d_array->index_to_entity[args.job_index];
            if (!transform_array->HasData(entity) || !material_array->HasData(entity))
            {
                return;
            }

            Sprite3DComponent& sprite = sprite_3d_array->data[args.job_index];
            const MaterialComponent& material = material_array->GetData(entity);
            if (material.GetMaterialSlotCount() == 0)
            {
                return;
            }
            const MaterialSlot& material_slot = material.material_slots[0];

            Scene::RenderData::Sprite3DRenderable renderable = {};
            renderable.instance_index = static_cast<uint32>(transform_array->entity_to_index[entity]);
            renderable.material_index = material.material_offset;
            renderable.size = sprite.size;
            renderable.pivot = sprite.pivot;
            renderable.uv_rect = sprite.uv_rect;
            if (sprite.IsBillboard())
            {
                renderable.flags |= Scene::RenderData::Sprite3DRenderable::Billboard;
            }
            if ((material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0)
            {
                renderable.flags |= Scene::RenderData::Sprite3DRenderable::Transparent;
            }

            bucket.sprite_3d_renderables.push_back(renderable);
            sprite.SetDirty(false);
        });
        jobsystem::Wait(sub_ctx);

        Size sprite_3d_renderable_count = 0;
        for (const SpriteBucket& bucket : sprite_buckets)
        {
            sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
        }

        render_data.sprite_3d_renderables.reserve(sprite_3d_renderable_count);
        for (SpriteBucket& bucket : sprite_buckets)
        {
            render_data.sprite_3d_renderables.insert(render_data.sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
        }
    }
}
