#pragma once

#include "definition.h"

namespace VulkanTest
{
namespace GPU
{
class DeviceVulkan;
class Event;

struct EventDeleter
{
    void operator()(Event* ent);
};
class Event : public IntrusivePtrEnabled<Event, EventDeleter>, public InternalSyncObject
{
public:
    Event(DeviceVulkan& device_, VkEvent ent_);
    ~Event();

    VkEvent GetEvent()const
    {
        return ent;
    }

    VkPipelineStageFlags GetStages()const
    {
        return stages;
    }

    void SetStages(VkPipelineStageFlags stages_)
    {
        stages = stages_;
    }

private:
    friend class DeviceVulkan;
    friend struct EventDeleter;
    friend class Util::ObjectPool<Event>;

    DeviceVulkan& device;
    VkEvent ent;
    VkPipelineStageFlags stages = 0;
};
using EventPtr = IntrusivePtr<Event>;

class EventManager
{
public:
    ~EventManager();

    void Initialize(DeviceVulkan& device_);
    VkEvent Requset();
    void Recyle(VkEvent ent);

private:
    DeviceVulkan* device = nullptr;
    std::vector<VkEvent> events;
};

}
}