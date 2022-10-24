#include "vk_textures.h"

#include "vk_engine.h"
#include "vk_initializers.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace vkutil {

bool load_image_from_file(const char *file, VulkanEngine &engine,
                          AllocatedImage &outImage) {
  int width, height, nChannels;

  stbi_uc *pixel_ptr =
      stbi_load(file, &width, &height, &nChannels, STBI_rgb_alpha);

  if (!pixel_ptr) {
    CORE_WARN("Failed to load texture file: {}", file);
    return false;
  }

  VkDeviceSize imageSize = width * height * 4;

  VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB;

  AllocatedBuffer stagingBuffer = engine.create_buffer(
      imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

  void *data;
  vmaMapMemory(engine.m_Allocator, stagingBuffer.allocation, &data);
  memcpy(data, pixel_ptr, static_cast<size_t>(imageSize));
  vmaUnmapMemory(engine.m_Allocator, stagingBuffer.allocation);

  stbi_image_free(pixel_ptr);

  VkExtent3D imageExtent;
  imageExtent.width = static_cast<uint32_t>(width);
  imageExtent.height = static_cast<uint32_t>(height);
  imageExtent.depth = 1;

  VkImageCreateInfo dimgInfo = vkinit::image_create_info(
      imageFormat, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      imageExtent);

  AllocatedImage newImage;

  VmaAllocationCreateInfo dimgAllocInfo{};
  dimgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

  vmaCreateImage(engine.m_Allocator, &dimgInfo, &dimgAllocInfo, &newImage.image,
                 &newImage.allocation, nullptr);

  engine.immediate_submit([&](VkCommandBuffer cmd) {
    VkImageSubresourceRange range;
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;

    VkImageMemoryBarrier imageBarrierToTransfer{};
    imageBarrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    imageBarrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageBarrierToTransfer.image = newImage.image;
    imageBarrierToTransfer.subresourceRange = range;

    imageBarrierToTransfer.srcAccessMask = 0;
    imageBarrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &imageBarrierToTransfer);

    VkBufferImageCopy copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = 0;
    copyRegion.bufferImageHeight = 0;

    copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyRegion.imageSubresource.mipLevel = 0;
    copyRegion.imageSubresource.baseArrayLayer = 0;
    copyRegion.imageSubresource.layerCount = 1;
    copyRegion.imageExtent = imageExtent;

    vkCmdCopyBufferToImage(cmd, stagingBuffer.buffer, newImage.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                           &copyRegion);

    VkImageMemoryBarrier imageBarrierToReadable = imageBarrierToTransfer;

    imageBarrierToReadable.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageBarrierToReadable.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    imageBarrierToReadable.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    imageBarrierToReadable.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &imageBarrierToReadable);
  });

  engine.m_MainDeletionQueue.push_function([=]() {
    vmaDestroyImage(engine.m_Allocator, newImage.image, newImage.allocation);
  });

	vmaDestroyBuffer(engine.m_Allocator, stagingBuffer.buffer, stagingBuffer.allocation);

	outImage = newImage;

	CORE_TRACE("loaded image: {}, with handle: {}", file, (void*)outImage.image);
  return true;
}
} // namespace vkutil