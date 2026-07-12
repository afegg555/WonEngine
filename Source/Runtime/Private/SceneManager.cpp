#include "SceneManager.h"

#include "Scene.h"

namespace won
{
    SceneManager::~SceneManager() = default;

    ecs::Scene& SceneManager::CreateScene(const ecs::SceneDesc& desc)
    {
        scenes.push_back(std::make_unique<ecs::Scene>(desc));
        return *scenes.back();
    }

    void SceneManager::DestroyScene(ecs::Scene* scene)
    {
        if (!scene)
        {
            return;
        }

        for (auto it = scenes.begin(); it != scenes.end(); ++it)
        {
            if (it->get() == scene)
            {
                scenes.erase(it);
                return;
            }
        }
    }

    const Vector<std::unique_ptr<ecs::Scene>>& SceneManager::GetScenes() const
    {
        return scenes;
    }
}
