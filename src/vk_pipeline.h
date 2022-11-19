#pragma once

#include "vk_types.h"
#include "vk_initializers.h"

#include <glm/glm.hpp>

namespace vkutil {

	class VulkanManager;

	class PipelineLayoutCache {
	public:

		PipelineLayoutCache() = default;
		PipelineLayoutCache(VkDevice device)
			: m_Device(device) {}

		VkPipelineLayout create_pipeline_layout(VkPipelineLayoutCreateInfo &info);

		void cleanup();

		struct PipelineLayoutInfo {
			std::vector<VkDescriptorSetLayout> m_Layouts;

			bool operator==(const PipelineLayoutInfo &other) const;

			size_t hash() const;
		};

	private:

		struct PipelineLayoutHash {
			std::size_t operator()(const PipelineLayoutInfo &k) const {
				return k.hash();
			}
		};

		VkDevice m_Device{ VK_NULL_HANDLE };
		std::unordered_map<PipelineLayoutInfo, VkPipelineLayout, PipelineLayoutHash> m_LayoutCache;

	};

	struct VertexInputDescription;

	class PipelineBuilder {
	public:

		PipelineBuilder(VulkanManager &manager);

		PipelineBuilder &add_shader_module(VkShaderModule shaderModule, VkShaderStageFlagBits shaderType);

		PipelineBuilder &set_renderpass(VkRenderPass renderpass);

		PipelineBuilder &set_vertex_description(VertexInputDescription &descriptrion);
		PipelineBuilder &PipelineBuilder::set_vertex_description(
			std::vector<VkVertexInputAttributeDescription> &attributes,
			std::vector<VkVertexInputBindingDescription> &bindings);
		PipelineBuilder &set_vertex_description(
			VkVertexInputAttributeDescription *pAttributes, uint32_t attributesCount,
			VkVertexInputBindingDescription *pBindings, uint32_t bindingCount);

		PipelineBuilder &set_depth_stencil(bool depthTest, bool depthWrite, VkCompareOp compareOp);

		PipelineBuilder &set_descriptor_layouts(std::initializer_list<VkDescriptorSetLayout> layouts);

		bool build(VkPipeline *pipeline, VkPipelineLayout *layout);
		bool build(VkPipeline *pipeline);

	private:

		VkDevice m_Device{ VK_NULL_HANDLE };
		VkRenderPass m_RenderPass{ VK_NULL_HANDLE };

		PipelineLayoutCache *m_LayoutCache{ nullptr };

		std::vector<VkPipelineShaderStageCreateInfo> m_ShaderStageInfo;
		VkPipelineVertexInputStateCreateInfo m_VertexInputInfo{};

		std::vector<VkDescriptorSetLayout> m_DescriptorSetLayout;

		bool m_EnableDepthStencil = false;
		VkPipelineDepthStencilStateCreateInfo m_DepthStencil{};

	};

} //namespace vkutil
