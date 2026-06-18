#include "EventHandler.h"

#include <list>
#include <mutex>
#include <unordered_map>

namespace won::eventhandler
{
    struct PendingEvent
    {
        uint64 id;
        function::Value payload;
        String owned_string;
    };

    struct EventManager
    {
        UnorderedMap<uint64, std::list<std::function<void(const function::Value&)>*>> subscribers;
        UnorderedMap<uint64, std::list<std::function<void(const function::Value&)>>> subscribers_once;
        Vector<PendingEvent> pending;
        std::mutex locker;
    };

    static std::shared_ptr<EventManager> manager = std::make_shared<EventManager>();

    struct EventInternal
    {
        std::shared_ptr<EventManager> manager;
        uint64 id = 0;
        std::function<void(const function::Value&)> callback;

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

    Handle Subscribe(uint64 id, std::function<void(const function::Value&)> callback)
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

    void SubscribeOnce(uint64 id, std::function<void(const function::Value&)> callback)
    {
        std::scoped_lock lock(manager->locker);
        manager->subscribers_once[id].push_back(std::move(callback));
    }

    void FireEvent(uint64 id, const function::Value& payload)
    {
        manager->locker.lock();

        {
            auto iter = manager->subscribers_once.find(id);
            if (iter != manager->subscribers_once.end())
            {
                auto& callbacks = iter->second;
                for (auto& callback : callbacks)
                {
                    auto cb = std::move(callback);
                    manager->locker.unlock();
                    cb(payload);
                    manager->locker.lock();
                }
                callbacks.clear();
            }
        }

        {
            auto iter = manager->subscribers.find(id);
            if (iter != manager->subscribers.end())
            {
                auto& callbacks = iter->second;
                for (auto* callback : callbacks)
                {
                    auto cb = *callback;
                    manager->locker.unlock();
                    cb(payload);
                    manager->locker.lock();
                }
            }
        }

        manager->locker.unlock();
    }

    void PostEvent(uint64 id, const function::Value& payload)
    {
        PendingEvent ev;
        ev.id = id;
        ev.payload = payload;
        if (payload.type == won::ValueType::String && payload.string_value)
        {
            ev.owned_string = payload.string_value;
            ev.payload.string_value = ev.owned_string.c_str();
        }
        std::scoped_lock lock(manager->locker);
        manager->pending.push_back(std::move(ev));
    }

    void Dispatch()
    {
        Vector<PendingEvent> current;
        {
            std::scoped_lock lock(manager->locker);
            current = std::move(manager->pending);
        }
        for (const PendingEvent& ev : current)
            FireEvent(ev.id, ev.payload);
    }
}
