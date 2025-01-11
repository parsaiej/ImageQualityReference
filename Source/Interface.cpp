#include "vulkan/vulkan_core.h"
#include <Interface.h>
#include <State.h>
#include <spdlog/spdlog.h>

// Vulkan Utility + State
// -------------------------------------

static VkDescriptorPool sVkInterfaceDescriptorPool = VK_NULL_HANDLE;
static VkQueue          sVkInterfaceQueue          = VK_NULL_HANDLE;

void CheckVK(VkResult r)
{
    if (r != VK_SUCCESS)
    {
        spdlog::critical("Interface Fatal Error: {}: ", string_VkResult(r));
        exit(1);
    }
}

// NOTE: Our vcpkg port of imgui is behind the current version which contains this helper.
// We just paste it here for now.
uint32_t ImGui_ImplVulkanH_SelectQueueFamilyIndex(VkPhysicalDevice physical_device)
{
    uint32_t count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
    ImVector<VkQueueFamilyProperties> queues_properties;
    queues_properties.resize((int)count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queues_properties.Data);
    for (uint32_t i = 0; i < count; i++)
        if (queues_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            return i;
    return (uint32_t)-1;
}

// -------------------------------------

void ImageQualityReference::CreateInterface()
{
    if (ImGui::GetCurrentContext() != nullptr)
        DestroyInterface();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    auto ImInitializeVulkan = []()
    {
        auto vkInstance       = static_cast<VkInstance>(gNRI.GetInstanceVK(*gDevice));
        auto vkPhysicalDevice = static_cast<VkPhysicalDevice>(gNRI.GetPhysicalDeviceVK(*gDevice));
        auto vkDevice         = static_cast<VkDevice>(gNRI.GetDeviceVK(*gDevice));

        VkDescriptorPoolSize interfaceDescriptorPoolSizes[] = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 },
        };

        VkDescriptorPoolCreateInfo interfaceDescriptorPoolInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        {
            interfaceDescriptorPoolInfo.maxSets       = 2;
            interfaceDescriptorPoolInfo.poolSizeCount = 1;
            interfaceDescriptorPoolInfo.pPoolSizes    = interfaceDescriptorPoolSizes;
        }
        CheckVK(vkCreateDescriptorPool(vkDevice, &interfaceDescriptorPoolInfo, nullptr, &sVkInterfaceDescriptorPool));

        auto queueFamilyIndex = ImGui_ImplVulkanH_SelectQueueFamilyIndex(vkPhysicalDevice);
        vkGetDeviceQueue(vkDevice, queueFamilyIndex, 0u, &sVkInterfaceQueue);

        VkFormat backBufferFormat = VK_FORMAT_R8G8B8A8_UNORM;

        VkPipelineRenderingCreateInfoKHR dynamicRenderingPipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
        {
            dynamicRenderingPipelineInfo.colorAttachmentCount    = 1;
            dynamicRenderingPipelineInfo.pColorAttachmentFormats = &backBufferFormat;
            dynamicRenderingPipelineInfo.viewMask                = 0;
        }

        ImGui_ImplVulkan_InitInfo infoVK = {};
        {
            infoVK.Instance                    = vkInstance;
            infoVK.PhysicalDevice              = vkPhysicalDevice;
            infoVK.Device                      = vkDevice;
            infoVK.QueueFamily                 = queueFamilyIndex;
            infoVK.Queue                       = sVkInterfaceQueue;
            infoVK.DescriptorPool              = sVkInterfaceDescriptorPool;
            infoVK.RenderPass                  = VK_NULL_HANDLE;
            infoVK.ImageCount                  = 2;
            infoVK.MinImageCount               = 2;
            infoVK.PipelineRenderingCreateInfo = dynamicRenderingPipelineInfo;
            infoVK.UseDynamicRendering         = true;
            infoVK.CheckVkResultFn             = CheckVK;
        }
        ImGui_ImplVulkan_Init(&infoVK);
    };

#ifdef __APPLE__
    // For MoltenVK we use VK + GLFW so can use the combined utility here.
    ImGui_ImplGlfw_InitForVulkan(gWindow, true);

    ImInitializeVulkan();
#else
    // TODO

    ImGui_ImplGlfw_InitForOther(gWindow, true);
#endif
}

void ImageQualityReference::DestroyInterface()
{
    // TODO
    ImGui::DestroyContext();
}
