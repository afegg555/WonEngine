#pragma once
#include "Entity.h"
#include "ReflectionTypes.h"
#include "TerrainData.h"
#include "Types.h"

#include <memory>

namespace won::ecs
{
    class Scene;
}

namespace won::editor
{
    struct EditorContext
    {
        ecs::Scene* scene = nullptr;
        String content_root;
    };

    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;
        virtual ecs::Entity Undo(EditorContext& context) = 0;
        virtual ecs::Entity Redo(EditorContext& context) = 0;
        virtual const String& GetName() const = 0;
    };

    struct ComponentState
    {
        won::TypeId type_id = 0;
        bool existed = false;
        String blob;
    };

    struct TerrainSampleState
    {
        Size index = 0;
        float height_delta = 0.0f;
        uint8 flatten_mask = 0;
        float flatten_height = 0.0f;
    };

    struct TerrainSampleChange
    {
        TerrainSampleState before;
        TerrainSampleState after;
    };

    class TerrainEditCommand : public EditorCommand
    {
    public:
        TerrainEditCommand(ecs::Entity entity, String terrain_data_path, Vector<TerrainSampleChange> sample_changes, String name);
        TerrainEditCommand(ecs::Entity entity, String terrain_data_path, Vector<terrain::TerrainSpline> before_splines, Vector<terrain::TerrainSpline> after_splines, String name);

        ecs::Entity Undo(EditorContext& context) override;
        ecs::Entity Redo(EditorContext& context) override;
        const String& GetName() const override
        {
            return name;
        }

    private:
        ecs::Entity Apply(EditorContext& context, bool use_before);

        ecs::Entity entity = ecs::INVALID_ENTITY;
        String terrain_data_path;
        Vector<TerrainSampleChange> sample_changes;
        Vector<terrain::TerrainSpline> before_splines;
        Vector<terrain::TerrainSpline> after_splines;
        String name;
        bool replaces_splines = false;
    };

    class ComponentEditCommand : public EditorCommand
    {
    public:
        ComponentEditCommand(ecs::Entity entity, Vector<ComponentState> before, Vector<ComponentState> after, String name);

        ecs::Entity Undo(EditorContext& context) override;
        ecs::Entity Redo(EditorContext& context) override;
        const String& GetName() const override
        {
            return name;
        }

    private:
        ecs::Entity Apply(EditorContext& context, const Vector<ComponentState>& states);

        ecs::Entity entity = ecs::INVALID_ENTITY;
        Vector<ComponentState> before;
        Vector<ComponentState> after;
        String name;
    };

	class EntityLifetimeCommand : public EditorCommand // creates or destroys an entity and its subtree
    {
    public:
        EntityLifetimeCommand(ecs::Entity root, String before, String after, String name);

        ecs::Entity Undo(EditorContext& context) override;
        ecs::Entity Redo(EditorContext& context) override;
        const String& GetName() const override
        {
            return name;
        }

    private:
        ecs::Entity Apply(EditorContext& context, const String& snapshot_blob);

        ecs::Entity root = ecs::INVALID_ENTITY;
        String before;
        String after;
        String name;
    };

    class EditorHistory
    {
    public:
        static Vector<ComponentState> CaptureComponents(ecs::Scene& scene, ecs::Entity entity);
        static String CaptureSubtree(ecs::Scene& scene, ecs::Entity root);

        void PushComponentEdit(ecs::Scene& scene, ecs::Entity entity, Vector<ComponentState> before, const String& fallback_name);
        void PushEntityLifetime(ecs::Scene& scene, ecs::Entity root, String before_blob, String name);
        void PushTerrainSamples(ecs::Entity entity, String terrain_data_path, Vector<TerrainSampleChange> changes, String name);
        void PushTerrainSplines(ecs::Entity entity, String terrain_data_path, Vector<terrain::TerrainSpline> before, Vector<terrain::TerrainSpline> after, String name);

        ecs::Entity Undo(EditorContext& context);
        ecs::Entity Redo(EditorContext& context);

        bool CanUndo() const
        {
            return !undo_stack.empty();
        }
        bool CanRedo() const
        {
            return !redo_stack.empty();
        }
        const String& PeekUndoName() const;
        const String& PeekRedoName() const;

        void Clear();

    private:
        void Push(std::unique_ptr<EditorCommand> command);

        Vector<std::unique_ptr<EditorCommand>> undo_stack;
        Vector<std::unique_ptr<EditorCommand>> redo_stack;
        Size max_depth = 100;
    };
}
