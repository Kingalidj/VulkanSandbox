#pragma once
#include "vk_types.h"

namespace vkutil {
	class VulkanManager;

	VkDescriptorImageInfo descriptor_image_info(VkTexture &texture);
	VkDescriptorBufferInfo descriptor_buffer_info(AllocatedBuffer &buffer, uint32_t size);
	std::vector<VkDescriptorImageInfo> descriptor_image_array_info(VkTexture *texture, uint32_t size);

	struct VkDescriptor {
		VkDescriptorSet set{ VK_NULL_HANDLE };
		VkDescriptorSetLayout layout{ VK_NULL_HANDLE };
	};

	class DescriptorAllocator {
	public:

		DescriptorAllocator() = default;

		DescriptorAllocator(VkDevice device)
			:m_Device(device) {
		}

		struct PoolSizes {
			std::vector<std::pair<VkDescriptorType, float>> sizes =
			{
				{ VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
				{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
				{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
			};
		};

		void reset_pools();
		bool allocate(VkDescriptorSet *set, VkDescriptorSetLayout layout);

		void cleanup();

		VkDevice m_Device{ VK_NULL_HANDLE };
	private:
		VkDescriptorPool grab_pool();

		VkDescriptorPool m_CurrentPool{ VK_NULL_HANDLE };
		PoolSizes m_DescriptorSizes;
		std::vector<VkDescriptorPool> m_UsedPools;
		std::vector<VkDescriptorPool> m_FreePools;
	};

	class DescriptorLayoutCache {
	public:

		DescriptorLayoutCache() = default;

		DescriptorLayoutCache(VkDevice device)
			: m_Device(device) {}

		void cleanup();

		VkDescriptorSetLayout create_descriptor_layout(VkDescriptorSetLayoutCreateInfo &info);

		struct DescriptorLayoutInfo {
			std::vector<VkDescriptorSetLayoutBinding> bindings;

			bool operator==(const DescriptorLayoutInfo &other) const;

			size_t hash() const;
		};

	private:

		struct DescriptorLayoutHash {
			std::size_t operator()(const DescriptorLayoutInfo &k) const {
				return k.hash();
			}
		};

		VkDevice m_Device{ VK_NULL_HANDLE };
		std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> m_LayoutCache;
	};

	class DescriptorBuilder {
	public:

		DescriptorBuilder(VulkanManager &manager);

		DescriptorBuilder(DescriptorLayoutCache *layoutCache, DescriptorAllocator *allocator)
			: m_LayoutCache(layoutCache), m_Alloc(allocator) {}

		DescriptorBuilder &bind_buffer(uint32_t binding, AllocatedBuffer &buffer, uint32_t size, VkDescriptorType type, VkShaderStageFlags flags);
		DescriptorBuilder &bind_image(uint32_t binding, VkTexture &image, VkDescriptorType type, VkShaderStageFlags flags);
		DescriptorBuilder &bind_image_array(uint32_t binding, VkTexture *images, uint32_t imageCount, VkDescriptorType type, VkShaderStageFlags flags);
		DescriptorBuilder &enable_push_descriptor();

		bool build(VkDescriptorSet *set, VkDescriptorSetLayout *layout);
		bool build(VkDescriptorSet *set);

		uint32_t get_layout_count();

	private:
		std::vector<VkWriteDescriptorSet> m_Writes;
		std::vector<VkDescriptorSetLayoutBinding> m_Bindings;

		uint32_t m_DescInfoCount{ 0 };
		std::unordered_map<uint32_t, VkDescriptorImageInfo> m_DescImageInfos;
		std::unordered_map<uint32_t, VkDescriptorBufferInfo> m_DescBufferInfos;
		std::unordered_map<uint32_t, std::vector<VkDescriptorImageInfo>> m_DescImageArrayInfos;
		bool m_Pushable{ false };

		DescriptorLayoutCache *m_LayoutCache;
		DescriptorAllocator *m_Alloc;


	};

	void descriptor_update_buffer(VulkanManager &manager, VkDescriptorSet *set, uint32_t binding,
		AllocatedBuffer &buffer, uint32_t size, VkDescriptorType type, VkShaderStageFlags flags);
	void descriptor_update_image(VulkanManager &manager, VkDescriptorSet *set, uint32_t binding,
		VkTexture &tex, VkDescriptorType type, VkShaderStageFlags flags);
	void descriptor_update_image_array(VulkanManager &manager, VkDescriptorSet *set, uint32_t binding,
		VkTexture *textures, uint32_t imgCount, VkDescriptorType type, VkShaderStageFlags flags);

} // namespace vkutil
