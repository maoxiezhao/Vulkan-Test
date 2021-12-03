#pragma once

#include "definition.h"
#include "memory.h"

namespace VulkanTest
{
namespace GPU
{
class DeviceVulkan;
class ImageView;
class Image;

enum ImageMiscFlagBits
{
    IMAGE_MISC_GENERATE_MIPS_BIT = 1 << 0,
    IMAGE_MISC_FORCE_ARRAY_BIT = 1 << 1,
    IMAGE_MISC_MUTABLE_SRGB_BIT = 1 << 2,
    IMAGE_MISC_CONCURRENT_QUEUE_GRAPHICS_BIT = 1 << 3,
    IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_COMPUTE_BIT = 1 << 4,
    IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_GRAPHICS_BIT = 1 << 5,
    IMAGE_MISC_CONCURRENT_QUEUE_ASYNC_TRANSFER_BIT = 1 << 6,
    IMAGE_MISC_VERIFY_FORMAT_FEATURE_SAMPLED_LINEAR_FILTER_BIT = 1 << 7,
    IMAGE_MISC_LINEAR_IMAGE_IGNORE_DEVICE_LOCAL_BIT = 1 << 8,
    IMAGE_MISC_FORCE_NO_DEDICATED_BIT = 1 << 9,
    IMAGE_MISC_NO_DEFAULT_VIEWS_BIT = 1 << 10
};

struct ImageViewDeleter
{
    void operator()(ImageView* imageView);
};
class ImageView : public Util::IntrusivePtrEnabled<ImageView, ImageViewDeleter>, public GraphicsCookie
{
public:
    ImageView(DeviceVulkan& device_, VkImageView imageView_, const ImageViewCreateInfo& info_);
    ~ImageView();

    VkImageView GetImageView()const
    {
        return imageView;
    }

	Image* GetImage()
	{
		return info.image;
	}

    const Image* GetImage() const
    {
        return info.image;
    }

    VkFormat GetFormat()const
    {
        return info.format;
    }

    const ImageViewCreateInfo& GetInfo()const
    {
        return info;
    }

    void SetDepthStencilView(VkImageView depth, VkImageView stencil)
    {
        depthView = depth;
        stencilView = stencil;
    }

    void SetRenderTargetViews(std::vector<VkImageView> views)
    {
        rtViews = views;
    }

    VkImageView GetRenderTargetView(uint32_t layer)const;

private:
    friend class DeviceVulkan;
    friend struct ImageViewDeleter;
    friend class Util::ObjectPool<ImageView>;
    
    DeviceVulkan& device;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageView depthView = VK_NULL_HANDLE;
    VkImageView stencilView = VK_NULL_HANDLE;
    std::vector<VkImageView> rtViews;
    ImageViewCreateInfo info;
};
using ImageViewPtr = Util::IntrusivePtr<ImageView>;

struct ImageDeleter
{
    void operator()(Image* image);
};
class Image : public Util::IntrusivePtrEnabled<Image, ImageDeleter>
{
public:
    ~Image();

    VkImage GetImage()const
    {
        return image;
    }

    ImageViewPtr GetImageViewPtr()
    {
        return imageView;
    }

    ImageView& GetImageView()
    {
        return *imageView;
    }

    const ImageView& GetImageView()const
    {
        return *imageView;
    }

    void DisownImge()
    {
        isOwnsImge = false;
    }

    VkImageLayout GetSwapchainLayout() const
    {
        return swapchainLayout;
    }

    void SetSwapchainLayout(VkImageLayout layout)
    {
        swapchainLayout = layout;
    }

    bool IsSwapchainImage()const
    {
        return swapchainLayout != VK_IMAGE_LAYOUT_UNDEFINED;
    }

    uint32_t GetWidth() const
    {
        return imageInfo.width;
    }

    uint32_t GetHeight() const
    {
        return imageInfo.height;
    }

    const ImageCreateInfo& GetCreateInfo()const
    {
        return imageInfo;
    }

    VkAccessFlags GetAccessFlags()const
    {
        return accessFlags;
    }

    VkPipelineStageFlags GetStageFlags()const
    {
        return stageFlags;
    }

    static VkPipelineStageFlags ConvertUsageToPossibleStages(VkImageUsageFlags usage);
    static VkAccessFlags ConvertUsageToPossibleAccess(VkImageUsageFlags usage);
    static VkAccessFlags ConvertLayoutToPossibleAccess(VkImageLayout layout);

private:
    friend class DeviceVulkan;
    friend struct ImageDeleter;
    friend class Util::ObjectPool<Image>;

    Image(DeviceVulkan& device_, VkImage image_, VkImageView imageView_, const DeviceAllocation& allocation_, const ImageCreateInfo& info_);

private:
    DeviceVulkan& device;
    VkImage image;
    ImageViewPtr imageView;
    ImageCreateInfo imageInfo;
    VkImageLayout swapchainLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    DeviceAllocation allocation;
    bool isOwnsImge = true;
    bool isOwnsMemory = true;

    VkAccessFlags accessFlags = 0;
    VkPipelineStageFlags stageFlags = 0;
};
using ImagePtr = Util::IntrusivePtr<Image>;

}
}