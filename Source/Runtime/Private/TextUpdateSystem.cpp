#include "TextUpdateSystem.h"

#include "MaterialComponent.h"
#include "Scene.h"
#include "StringUtils.h"
#include "Text3DComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <iterator>
#include <limits>

namespace won::ecs
{
    void TextUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct GlyphRequest
        {
            std::shared_ptr<resource::Font> font;
            uint32 codepoint = 0;
            uint32 pixel_height = 0;
        };

        struct TextBucket
        {
            Vector<GlyphRequest> glyph_requests;
            Vector<Scene::RenderData::Text3DRenderable> text_3d_renderables;
        };

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto text_3d_array = scene.GetComponentArray<Text3DComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        render_data.text_3d_renderables.clear();
        if (!text_3d_array || !transform_array || !material_array)
        {
            return;
        }

        jobsystem::Context sub_ctx;
        Vector<TextBucket> text_buckets(jobsystem::GetThreadCount() + 1);
        jobsystem::Dispatch(sub_ctx, static_cast<uint32>(text_3d_array->GetSize()), groupsize, [&](jobsystem::JobArgs args) {
            struct GlyphLayout
            {
                const resource::Font::Glyph* glyph = nullptr;
                uint32 codepoint = 0;
                uint32 line_index = 0;
                float pen_x = 0.0f;
            };

            TextBucket& bucket = text_buckets[args.worker_index];
            Text3DComponent& text = text_3d_array->data[args.job_index];

            if (!text.font || !text.font->IsValid() || text.pixel_height == 0)
            {
                return;
            }

            Vector<GlyphLayout> glyph_layouts;
            Vector<float> line_widths;
            line_widths.push_back(0.0f);
            const WString decoded_text = utils::DecodeUtf8(text.text);
            const float glyph_world_scale = text.height / static_cast<float>(text.pixel_height);
            uint32 line_index = 0;
            float pen_x = 0.0f;
            for (Size char_index = 0; char_index < decoded_text.size(); ++char_index)
            {
                uint32 codepoint = static_cast<uint32>(decoded_text[char_index]);
                if constexpr (sizeof(wchar_t) < 4)
                {
                    // use 2 * wchar_t (Emoji, etc.)
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF && char_index + 1 < decoded_text.size())
                    {
                        const uint32 low_surrogate = static_cast<uint32>(decoded_text[char_index + 1]);
                        if (low_surrogate >= 0xDC00 && low_surrogate <= 0xDFFF)
                        {
                            codepoint = (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00)) + 0x10000;
                            ++char_index;
                        }
                    }
                }
                if (codepoint == '\r' || codepoint == '\n')
                {
                    if (codepoint == '\r' && char_index + 1 < decoded_text.size() && decoded_text[char_index + 1] == '\n')
                    {
                        ++char_index;
                    }
                    line_widths[line_index] = pen_x;
                    line_widths.push_back(0.0f);
                    ++line_index;
                    pen_x = 0.0f;
                    continue;
                }

                const resource::Font::Glyph* glyph = text.font->atlas.FindGlyph(codepoint, text.pixel_height);
                if (!glyph)
                {
                    bucket.glyph_requests.push_back({ text.font, codepoint, text.pixel_height });
                    continue;
                }

                glyph_layouts.push_back({ glyph, codepoint, line_index, pen_x });
                pen_x += glyph->advance * glyph_world_scale;
            }
            line_widths[line_index] = pen_x;

            const Entity entity = text_3d_array->index_to_entity[args.job_index];
            if (transform_array && transform_array->HasData(entity))
            {
                const float font_metric_height = static_cast<float>(text.font->ascent - text.font->descent);
                const float font_metric_scale = font_metric_height > 0.0f ? static_cast<float>(text.pixel_height) / font_metric_height : 1.0f;
                const float line_advance = static_cast<float>(text.font->ascent - text.font->descent + text.font->line_gap) * font_metric_scale * glyph_world_scale;
                float visible_min_y = (std::numeric_limits<float>::max)();
                float visible_max_y = (std::numeric_limits<float>::lowest)();
                for (const GlyphLayout& layout : glyph_layouts)
                {
                    const resource::Font::Glyph* glyph = layout.glyph;
                    if (!glyph)
                    {
                        continue;
                    }
                    const float glyph_height = glyph->size.y * glyph_world_scale;
                    const float baseline_y = -line_advance * static_cast<float>(layout.line_index);
                    const float glyph_top_y = baseline_y - glyph->offset.y * glyph_world_scale;
                    visible_min_y = (std::min)(visible_min_y, glyph_top_y - glyph_height);
                    visible_max_y = (std::max)(visible_max_y, glyph_top_y);
                }
                const float text_visible_height = visible_max_y > visible_min_y ? visible_max_y - visible_min_y : 0.0f;
                const float pivot_y_offset = text_visible_height > 0.0f ? -(visible_min_y + text_visible_height * text.pivot.y) : 0.0f;
                for (const GlyphLayout& layout : glyph_layouts)
                {
                    const resource::Font::Glyph* glyph = layout.glyph;
                    if (!glyph)
                    {
                        continue;
                    }

                    Scene::RenderData::Text3DRenderable renderable = {};
                    renderable.instance_index = static_cast<uint32>(transform_array->entity_to_index[entity]);
                    if (material_array && material_array->HasData(entity) && material_array->GetData(entity).GetMaterialSlotCount() > 0)
                    {
                        renderable.material_index = material_array->GetData(entity).material_offset;
                    }
                    const float2 glyph_size = { glyph->size.x * glyph_world_scale, glyph->size.y * glyph_world_scale };
                    const float line_x = -line_widths[layout.line_index] * text.pivot.x;
                    const float glyph_visual_x = line_x + layout.pen_x + glyph->offset.x * glyph_world_scale;
                    const float baseline_y = pivot_y_offset - line_advance * static_cast<float>(layout.line_index);
                    const float glyph_top_y = baseline_y - glyph->offset.y * glyph_world_scale;
                    renderable.font = text.font;
                    renderable.position = { -glyph_visual_x - glyph_size.x, glyph_top_y - glyph_size.y };
                    renderable.size = glyph_size;
                    renderable.uv_rect = { glyph->uv_min.x, glyph->uv_min.y, glyph->uv_max.x, glyph->uv_max.y };
                    if (text.IsBillboard())
                    {
                        renderable.flags |= Scene::RenderData::Text3DRenderable::Billboard;
                    }
                    bucket.text_3d_renderables.push_back(renderable);
                }
            }

            text.SetDirty(false);
        });
        jobsystem::Wait(sub_ctx);

        Size text_3d_renderable_count = 0;
        for (const TextBucket& bucket : text_buckets)
        {
            text_3d_renderable_count += bucket.text_3d_renderables.size();
        }

        render_data.text_3d_renderables.reserve(text_3d_renderable_count);
        for (TextBucket& bucket : text_buckets)
        {
            render_data.text_3d_renderables.insert(render_data.text_3d_renderables.end(), std::make_move_iterator(bucket.text_3d_renderables.begin()), std::make_move_iterator(bucket.text_3d_renderables.end()));
        }

        Vector<std::shared_ptr<resource::Font>> dirty_fonts;
        for (TextBucket& bucket : text_buckets)
        {
            for (const GlyphRequest& request : bucket.glyph_requests)
            {
                if (!request.font || !request.font->atlas.RequestGlyph(request.codepoint, request.pixel_height))
                {
                    continue;
                }
                if (std::find(dirty_fonts.begin(), dirty_fonts.end(), request.font) == dirty_fonts.end())
                {
                    dirty_fonts.push_back(request.font);
                }
            }
        }

        for (const std::shared_ptr<resource::Font>& font : dirty_fonts)
        {
            resource::UpdateGlyphAtlas(*font);
        }
        // Newly packed glyphs are picked up on the next frame.
    }
}
