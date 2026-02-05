#include "EventHandler.h"

#include <list>
#include <mutex>
#include <unordered_map>

namespace won::eventhandler
{
    struct EventManager
    {
        UnorderedMap<int, std::list<std::function<void(uint64)>*>> subscribers;
        UnorderedMap<int, std::list<std::function<void(uint64)>>> subscribers_once;
        std::mutex locker;
    };

    static std::shared_ptr<EventManager> manager = std::make_shared<EventManager>();

    struct EventInternal
    {
        std::shared_ptr<EventManager> manager;
        int id = 0;
        std::function<void(uint64)> callback;

        ~EventInternal()
        {
            std::scoped_lock lock(manager->locker);
            auto iter = manager->subscribers.find(id);
            if (iter != manager->subscribers.end())
            {
                iter->second.remove(&callback);
            }
        }
    };

    Handle Subscribe(int id, std::function<void(uint64)> callback)
    {
        Handle handle;
        auto event_internal = std::make_shared<EventInternal>();
        handle.internal_state = event_internal;
        event_internal->manager = manager;
        event_internal->id = id;
        event_internal->callback = std::move(callback);

        std::scoped_lock lock(manager->locker);
        manager->subscribers[id].push_back(&event_internal->callback);

        return handle;
    }

    void SubscribeOnce(int id, std::function<void(uint64)> callback)
    {
        std::scoped_lock lock(manager->locker);
        manager->subscribers_once[id].push_back(std::move(callback));
    }

    void FireEvent(int id, uint64 userdata)
    {
        manager->locker.lock();

        {
            auto iter = manager->subscribers_once.find(id);
            bool found = iter != manager->subscribers_once.end();

            if (found)
            {
                auto& callbacks = iter->second;
                for (auto& callback : callbacks)
                {
                    auto cb = std::move(callback);
                    manager->locker.unlock();
                    cb(userdata);
                    manager->locker.lock();
                }
                callbacks.clear();
            }
        }

        {
            auto iter = manager->subscribers.find(id);
            bool found = iter != manager->subscribers.end();

            if (found)
            {
                auto& callbacks = iter->second;
                for (auto* callback : callbacks)
                {
                    auto cb = *callback;
                    manager->locker.unlock();
                    cb(userdata);
                    manager->locker.lock();
                }
            }
        }

        manager->locker.unlock();
    }
}
