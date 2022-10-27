#pragma once

#include "vk_scene.h"
#include "vk_types.h"
#include "vk_descriptors.h"
#include "vk_textures.h"

#include <glm/glm.hpp>

#include <imgui_impl_vulkan.h>
#include <imgui_impl_glfw.h>

struct UploadContext {
	VkFence uploadFence;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
};

struct GPUSceneData {
	//glm::vec4 fogColor;
	//glm::vec4 fogDistance;
	//glm::vec4 ambientColor;
	//glm::vec4 sunlightDirection;
	//glm::vec4 sunlightColor;
};

struct GPUCameraData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewProj;
};

struct GPUObjectData {
	glm::mat4 modelMatrix;
};

struct FrameData {
	VkSemaphore presentSemaphore, renderSemaphore;
	VkFence renderFence;

	VkCommandPool commandPool;
	VkCommandBuffer mainCommandBuffer;

	AllocatedBuffer cameraBuffer;
	AllocatedBuffer objectBuffer;

	VkDescriptorSet globalDescriptor;
	VkDescriptorSet objectDescriptor;
};

/* struct MeshPushConstants { */
/*   glm::vec4 data; */
/*   glm::mat4 renderMatrix; */
/* }; */

struct DeletionQueue {

	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()> &&func) { deletors.push_back(func); }

	void flush() {
		for (auto it = deletors.rbegin(); it != deletors.rend(); it++) {
			(*it)();
		}

		deletors.clear();
	}
};

//constexpr uint32_t FRAME_OVERLAP = 2;

class  VulkanEngine {
public:
	bool m_IsInitialized{ false };
	int m_FrameNumber{ 0 };

	VkExtent2D m_WindowExtent{ 1600, 900 };

	struct GLFWwindow *m_Window = nullptr;

	void init();
	void cleanup();
	void draw();
	void run();

	VkInstance m_Instance;
	VkDebugUtilsMessengerEXT m_DebugMessenger;
	VkPhysicalDevice m_PhysicalDevice;
	VkDevice m_Device;
	VkSurfaceKHR m_Surface;
	VkPhysicalDeviceProperties m_GPUProperties;

	VkSwapchainKHR m_Swapchain;
	VkFormat m_SwapchainImageFormat;
	VkFormat m_DepthFormat;

	std::vector<VkImage> m_SwapchainImages;
	std::vector<VkImageView> m_SwapchainImageViews;

	VkQueue m_GraphicsQueue;
	uint32_t m_GraphicsQueueFamily;

	FrameData m_FrameData;
	//FrameData &get_current_frame();

	VkRenderPass m_RenderPass;
	VkRenderPass m_ImGuiRenderPass;

	std::vector<VkFramebuffer> m_FrameBuffers;
	std::vector<VkFramebuffer> m_ImGuiFrameBuffers;

	/* VkSemaphore m_PresentSemaphore, m_RenderSemaphore; */
	/* VkFence m_RenderFence; */

	VkPipelineLayout m_TrianglePipelineLayout;
	VkPipelineLayout m_MeshPipelineLayout;
	VkPipeline m_MeshPipeline;

	DeletionQueue m_MainDeletionQueue;

	Mesh m_MonkeyMesh;
	Mesh m_TriangleMesh;

	VkImageView m_DepthImageView;
	AllocatedImage m_DepthImage;


	std::vector<RenderObject> m_RenderObjects;

	std::unordered_map<std::string, Material> m_Materials;
	std::unordered_map<std::string, Mesh> m_Meshes;
	std::unordered_map<std::string, Texture> m_Textures;

	VmaAllocator m_Allocator;

	VkDescriptorSetLayout m_GlobalSetLayout;
	VkDescriptorSetLayout m_ObjectSetLayout;
	VkDescriptorSetLayout m_SingleTextureSetLayout;

	VkDescriptorPool m_DescriptorPool;

	GPUSceneData m_SceneParameters;
	AllocatedBuffer m_SceneParameterBuffer;

	UploadContext m_UploadContext;

	vkutil::DescriptorAllocator m_DescriptorAllocator;
	vkutil::DescriptorLayoutCache m_DescriptorLayoutCache;

	Material *create_material(VkPipeline pipeline, VkPipelineLayout layout,
		const std::string &name);

	AllocatedBuffer create_buffer(size_t allocSize, VkBufferUsageFlags usage,
		VmaMemoryUsage memoryUsage);

	Material *get_material(const std::string &name);
	Mesh *get_mesh(const std::string &name);

	void draw_objects(VkCommandBuffer cmd, RenderObject *first, int count);

	size_t pad_uniform_buffer_size(size_t originalSize);

	void immediate_submit(std::function<void(VkCommandBuffer cmd)> &&function);

private:
	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_default_renderpass();
	void init_framebuffers();
	void init_sync_structures();
	void init_pipelines();
	void init_scene();
	void init_descriptors();
	void init_imgui();

	void load_meshes();
	void load_images();
	void upload_mesh(Mesh &mesh);
};
