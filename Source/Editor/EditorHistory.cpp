#include "EditorHistory.h"

#include "FileSystem.h"
#include "JsonArchive.h"
#include "Reflection.h"
#include "ResourceAsset.h"
#include "Scene.h"
#include "SceneComponents.h"
#include "SceneSerializer.h"

#include <algorithm>
#include <utility>

namespace won::editor
{
    static const String empty_name;

    TerrainEditCommand::TerrainEditCommand(ecs::Entity entity_in, String terrain_data_path_in, Vector<TerrainSampleChange> sample_changes_in, String name_in)
        : entity(entity_in)
        , terrain_data_path(std::move(terrain_data_path_in))
        , sample_changes(std::move(sample_changes_in))
        , name(std::move(name_in))
    {
    }

    TerrainEditCommand::TerrainEditCommand(ecs::Entity entity_in, String terrain_data_path_in, Vector<terrain::TerrainSpline> before_splines_in, Vector<terrain::TerrainSpline> after_splines_in, String name_in)
        : entity(entity_in)
        , terrain_data_path(std::move(terrain_data_path_in))
        , before_splines(std::move(before_splines_in))
        , after_splines(std::move(after_splines_in))
        , name(std::move(name_in))
        , replaces_splines(true)
    {
    }

    ecs::Entity TerrainEditCommand::Undo(EditorContext& context)
    {
        return Apply(context, true);
    }

    ecs::Entity TerrainEditCommand::Redo(EditorContext& context)
    {
        return Apply(context, false);
    }

    ecs::Entity TerrainEditCommand::Apply(EditorContext& context, bool use_before)
    {
        if (!context.scene || entity == ecs::INVALID_ENTITY || !context.scene->IsEntityAlive(entity))
        {
            return ecs::INVALID_ENTITY;
        }

        ecs::TerrainComponent* terrain = context.scene->GetComponent<ecs::TerrainComponent>(entity);
        if (!terrain || !terrain->data)
        {
            return ecs::INVALID_ENTITY;
        }

        terrain::TerrainData& data = *terrain->data;
        if (replaces_splines)
        {
            data.splines = use_before ? before_splines : after_splines;
        }
        else
        {
            for (const TerrainSampleChange& change : sample_changes)
            {
                const TerrainSampleState& state = use_before ? change.before : change.after;
                if (state.index >= data.height_delta.size() || state.index >= data.flatten_mask.size() || state.index >= data.flatten_height.size())
                {
                    return ecs::INVALID_ENTITY;
                }
            }
            for (const TerrainSampleChange& change : sample_changes)
            {
                const TerrainSampleState& state = use_before ? change.before : change.after;
                data.height_delta[state.index] = state.height_delta;
                data.flatten_mask[state.index] = state.flatten_mask;
                data.flatten_height[state.index] = state.flatten_height;
            }
        }

        terrain::CompositeTerrainHeights(data);
        if (!terrain_data_path.empty())
        {
            terrain::SaveTerrainBinary(io::CombinePath(context.content_root, terrain_data_path), data);
        }
        if (ecs::Collider3DComponent* collider = context.scene->GetComponent<ecs::Collider3DComponent>(entity))
        {
            collider->SetDirty();
        }
        context.scene->SetBVHDirty();
        return entity;
    }

    ComponentEditCommand::ComponentEditCommand(ecs::Entity entity_in, Vector<ComponentState> before_in, Vector<ComponentState> after_in, String name_in)
        : entity(entity_in)
        , before(std::move(before_in))
        , after(std::move(after_in))
        , name(std::move(name_in))
    {
    }

    ecs::Entity ComponentEditCommand::Undo(EditorContext& context)
    {
        return Apply(context, before);
    }

    ecs::Entity ComponentEditCommand::Redo(EditorContext& context)
    {
        return Apply(context, after);
    }

    ecs::Entity ComponentEditCommand::Apply(EditorContext& context, const Vector<ComponentState>& states)
    {
        if (!context.scene || entity == ecs::INVALID_ENTITY || !context.scene->IsEntityAlive(entity))
        {
            return ecs::INVALID_ENTITY;
        }

        for (const ComponentState& state : states)
        {
            if (state.existed)
            {
                serialize::JsonArchive archive(serialize::ArchiveMode::Read);
                if (!state.blob.empty() && archive.LoadFromString(state.blob))
                {
                    serialize::LoadComponent(archive, *context.scene, entity, state.type_id);
                }
            }
            else
            {
                context.scene->RemoveComponent(entity, state.type_id);
            }
        }

        Vector<ecs::Entity> entities;
        entities.push_back(entity);
        resource::LoadEntityResources(*context.scene, context.content_root, entities);
        return entity;
    }

    EntityLifetimeCommand::EntityLifetimeCommand(ecs::Entity root_in, String before_in, String after_in, String name_in)
        : root(root_in)
        , before(std::move(before_in))
        , after(std::move(after_in))
        , name(std::move(name_in))
    {
    }

    ecs::Entity EntityLifetimeCommand::Undo(EditorContext& context)
    {
        return Apply(context, before);
    }

    ecs::Entity EntityLifetimeCommand::Redo(EditorContext& context)
    {
        return Apply(context, after);
    }

    ecs::Entity EntityLifetimeCommand::Apply(EditorContext& context, const String& snapshot_blob)
    {
        if (!context.scene)
        {
            return ecs::INVALID_ENTITY;
        }

        ecs::Scene& scene = *context.scene;
        if (root != ecs::INVALID_ENTITY && scene.IsEntityAlive(root))
        {
            scene.DestroyEntity(root);
        }

        if (snapshot_blob.empty())
        {
            scene.SetBVHDirty();
            return ecs::INVALID_ENTITY;
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromString(snapshot_blob))
        {
            return ecs::INVALID_ENTITY;
        }

        Vector<ecs::Entity> new_entities;
        if (serialize::LoadEntitySnapshot(archive, scene, new_entities) == ecs::INVALID_ENTITY)
        {
            return ecs::INVALID_ENTITY;
        }

        resource::LoadEntityResources(scene, context.content_root, new_entities);
        return root;
    }

	Vector<ComponentState> EditorHistory::CaptureComponents(ecs::Scene& scene, ecs::Entity entity) // returns a vector of all component states for the given entity
    {
        Vector<ComponentState> states;
        if (entity == ecs::INVALID_ENTITY || !scene.IsEntityAlive(entity))
        {
            return states;
        }

        const Vector<const won::TypeDesc*> component_types = scene.GetComponentTypes();
        for (const won::TypeDesc* type_desc : component_types)
        {
            if (!type_desc || !scene.HasComponent(entity, type_desc->type_id))
            {
                continue;
            }

            ComponentState state;
            state.type_id = type_desc->type_id;
            state.existed = true;
            serialize::JsonArchive archive(serialize::ArchiveMode::Write);
            if (!serialize::SaveComponent(archive, scene, entity, type_desc->type_id) || !archive.SaveToString(state.blob))
            {
                state.blob.clear();
            }
            states.push_back(std::move(state));
        }
        return states;
    }

    String EditorHistory::CaptureSubtree(ecs::Scene& scene, ecs::Entity root)
    {
        if (root == ecs::INVALID_ENTITY || !scene.IsEntityAlive(root))
        {
            return String();
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        String blob;
        if (!serialize::SaveEntitySnapshot(archive, scene, root) || !archive.SaveToString(blob))
        {
            return String();
        }
        return blob;
    }

    void EditorHistory::PushComponentEdit(ecs::Scene& scene, ecs::Entity entity, Vector<ComponentState> before, const String& fallback_name)
    {
        if (entity == ecs::INVALID_ENTITY || !scene.IsEntityAlive(entity))
        {
            return;
        }

        Vector<ComponentState> after_components = CaptureComponents(scene, entity);

        Vector<ComponentState> command_before;
        Vector<ComponentState> command_after;
        for (const ComponentState& before_state : before)
        {
            const auto after_it = std::find_if(after_components.begin(), after_components.end(),
                [&](const ComponentState& state) { return state.type_id == before_state.type_id; });
            if (after_it == after_components.end())
            {
                command_before.push_back(before_state);
                command_after.push_back({ before_state.type_id, false, String() });
            }
            else if (after_it->blob != before_state.blob)
            {
                command_before.push_back(before_state);
                command_after.push_back(*after_it);
            }
        }
        for (const ComponentState& after_state : after_components)
        {
            const auto before_it = std::find_if(before.begin(), before.end(),
                [&](const ComponentState& state) { return state.type_id == after_state.type_id; });
            if (before_it == before.end())
            {
                command_before.push_back({ after_state.type_id, false, String() });
                command_after.push_back(after_state);
            }
        }

        if (command_before.empty())
        {
            return;
        }

        String name = fallback_name;
        if (command_before.size() == 1)
        {
            const won::TypeDesc* type_desc = reflection::FindType(command_before[0].type_id);
            if (type_desc && type_desc->name)
            {
                if (!command_before[0].existed)
                {
                    name = String("Add ") + type_desc->name;
                }
                else if (!command_after[0].existed)
                {
                    name = String("Remove ") + type_desc->name;
                }
                else
                {
                    name = String("Edit ") + type_desc->name;
                }
            }
        }

        Push(std::make_unique<ComponentEditCommand>(entity, std::move(command_before), std::move(command_after), std::move(name)));
    }

    void EditorHistory::PushEntityLifetime(ecs::Scene& scene, ecs::Entity root, String before_blob, String name)
    {
        if (root == ecs::INVALID_ENTITY)
        {
            return;
        }

        String after_blob = CaptureSubtree(scene, root);
        if (after_blob == before_blob)
        {
            return;
        }

        Push(std::make_unique<EntityLifetimeCommand>(root, std::move(before_blob), std::move(after_blob), std::move(name)));
    }

    void EditorHistory::PushTerrainSamples(ecs::Entity entity, String terrain_data_path, Vector<TerrainSampleChange> changes, String name)
    {
        if (entity == ecs::INVALID_ENTITY || changes.empty())
        {
            return;
        }
        Push(std::make_unique<TerrainEditCommand>(entity, std::move(terrain_data_path), std::move(changes), std::move(name)));
    }

    void EditorHistory::PushTerrainSplines(ecs::Entity entity, String terrain_data_path, Vector<terrain::TerrainSpline> before, Vector<terrain::TerrainSpline> after, String name)
    {
        if (entity == ecs::INVALID_ENTITY)
        {
            return;
        }
        Push(std::make_unique<TerrainEditCommand>(entity, std::move(terrain_data_path), std::move(before), std::move(after), std::move(name)));
    }

    void EditorHistory::Push(std::unique_ptr<EditorCommand> command)
    {
        redo_stack.clear();
        undo_stack.push_back(std::move(command));
        if (undo_stack.size() > max_depth)
        {
            undo_stack.erase(undo_stack.begin());
        }
    }

    ecs::Entity EditorHistory::Undo(EditorContext& context)
    {
        if (undo_stack.empty())
        {
            return ecs::INVALID_ENTITY;
        }

        std::unique_ptr<EditorCommand> command = std::move(undo_stack.back());
        undo_stack.pop_back();
        const ecs::Entity affected = command->Undo(context);
        redo_stack.push_back(std::move(command));
        return affected;
    }

    ecs::Entity EditorHistory::Redo(EditorContext& context)
    {
        if (redo_stack.empty())
        {
            return ecs::INVALID_ENTITY;
        }

        std::unique_ptr<EditorCommand> command = std::move(redo_stack.back());
        redo_stack.pop_back();
        const ecs::Entity affected = command->Redo(context);
        undo_stack.push_back(std::move(command));
        return affected;
    }

    const String& EditorHistory::PeekUndoName() const
    {
        return undo_stack.empty() ? empty_name : undo_stack.back()->GetName();
    }

    const String& EditorHistory::PeekRedoName() const
    {
        return redo_stack.empty() ? empty_name : redo_stack.back()->GetName();
    }

    void EditorHistory::Clear()
    {
        undo_stack.clear();
        redo_stack.clear();
    }
}
