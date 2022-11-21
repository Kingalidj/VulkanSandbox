#include "vk_engine.h"

#include "window.h"

#include "vk_initializers.h"
#include "vk_pipeline.h"
#include "vk_shader.h"
#include "vk_types.h"

#include "imgui_theme.h"

const uint8_t c_RobotoRegular[] = {
#include "robot_regular.embed"
};

#include <VkBootstrap.h>
#include <glm/gtx/transform.hpp>

#include <cmath>

namespace vkutil {

	VulkanEngine::VulkanEngine(Window &window)
		: m_App_Callback(window.get_event_callback()),
		m_WindowExtent({ window.get_width(), window.get_height() })
	{
		init_vulkan(window);
		init_swapchain();
		init_commands();
		init_renderpass();
		init_framebuffers();
		init_sync_structures();

		init_imgui(window);

		init_vp_renderpass();
		init_vp_framebuffers();

		init_descriptors();
		init_pipelines();


		load_images();
		load_meshes();

		init_scene();

		m_IsInitialized = true;
	}

	void VulkanEngine::draw_objects(VkCommandBuffer cmd, RenderObject *first,
		int count) {
		glm::vec3 camPos = { 0.f, -3.f, -9.f };

		glm::mat4 view = glm::translate(glm::mat4(1.f), camPos);
		glm::mat4 projection = glm::perspective(glm::radians(70.f),
			(float)m_ViewportExtent.width / m_ViewportExtent.height, 0.1f, 200.0f);
		projection[1][1] *= -1;

		glm::mat4 rotMat = glm::rotate(glm::mat4(1), glm::radians((float)m_FrameNumber * 2.0f), glm::vec3(0, 1, 0));

		GPUCameraData camData;
		camData.proj = projection;
		camData.view = view;
		camData.viewProj = projection * view;

		map_memory(m_VkManager, &m_FrameData.cameraBuffer, &camData, sizeof(GPUCameraData));

		float framed = (m_FrameNumber / 120.f);

		map_memory(m_VkManager, &m_FrameData.objectBuffer, [=](void *data) {
			GPUObjectData *objectSSBO = (GPUObjectData *)data;

		for (int i = 0; i < count; i++) {
			RenderObject &object = first[i];
			objectSSBO[i].modelMatrix = object.transformMatrix * rotMat;
		}
		});

		Ref<Mesh> lastMesh = nullptr;
		Ref<Material> lastMaterial = nullptr;
		for (int i = 0; i < count; i++) {
			RenderObject &object = first[i];

			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				object.material->pipeline);
			if (object.material != lastMaterial) {

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					object.material->pipeline);
				lastMaterial = object.material;

				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					object.material->pipelineLayout, 0, 1,
					&m_FrameData.cameraDescriptor, 0, nullptr);

				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					object.material->pipelineLayout, 1, 1,
					&m_FrameData.objectDescriptor, 0, nullptr);

				if (object.material->textureSet != VK_NULL_HANDLE) {

					//Ref<Texture> tex = m_VkManager.get_texture("empire_diffuse").value();

					//vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					//	object.material->pipelineLayout, 2, 1,
					//	&tex->descriptor, 0, nullptr);

					vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						object.material->pipelineLayout, 2, 1,
						&object.material->textureSet, 0, nullptr);
				}
			}

			/* glm::mat4 model = object.transformMatrix * rotMat; */
			/* glm::mat4 meshMatrix = projection * view * model; */

			/*
		MeshPushConstants constants;
		constants.renderMatrix = object.transformMatrix * rotMat;

		// upload the mesh to the GPU via push constants
		vkCmdPushConstants(cmd, object.material->pipelineLayout,
											 VK_SHADER_STAGE_VERTEX_BIT, 0,
		sizeof(MeshPushConstants), &constants);
			*/

			if (object.mesh != lastMesh) {
				VkDeviceSize offset = 0;
				vkCmdBindVertexBuffers(cmd, 0, 1, &object.mesh->vertexBuffer.buffer,
					&offset);
				lastMesh = object.mesh;
			}

			vkCmdDraw(cmd, (uint32_t)object.mesh->vertices.size(), 1, 0, i); // TODO: look at first instance
		}
	}

	void VulkanEngine::cleanup() {
		if (m_IsInitialized) {

			VK_CHECK(vkDeviceWaitIdle(m_Device));

			destroy_framebuffer(m_VkManager, m_ViewportFramebuffer);
			cleanup_swapchain();

			m_VkManager.cleanup();
			m_MainDeletionQueue.flush();

			//glfwDestroyWindow(m_Window);
			//m_Window->destroy();
			//glfwTerminate();
		}
	}

	void VulkanEngine::prepare_frame(uint32_t *swapchainImageIndex)
	{
		if (m_ViewportbufferResized) {
			m_ViewportbufferResized = false;
			rebuild_vp_framebuffer();
		}

		if (m_FramebufferResized) {
			m_FramebufferResized = false;
			rebuild_swapchain();
		}

		// we will write to this image index (framebuffer)
		VkResult res = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX,
			m_FrameData.presentSemaphore, nullptr,
			swapchainImageIndex);

		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
			m_FramebufferResized = false;
			rebuild_swapchain();
		}

		VK_CHECK(res);

		VK_CHECK(vkWaitForFences(m_Device, 1, &m_FrameData.renderFence, true, UINT64_MAX));
		VK_CHECK(vkResetFences(m_Device, 1, &m_FrameData.renderFence));

		// since we waited the buffer is empty
		VK_CHECK(vkResetCommandBuffer(m_FrameData.mainCommandBuffer, 0));

		VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

		VK_CHECK(vkBeginCommandBuffer(m_FrameData.mainCommandBuffer, &cmdBeginInfo));

		{
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
		}
	}

	void VulkanEngine::end_frame(uint32_t swapchainImageIndex)
	{
		VkCommandBuffer cmd = m_FrameData.mainCommandBuffer;

		VK_CHECK(vkEndCommandBuffer(cmd));

		VkSubmitInfo submitInfo = vkinit::submit_info(&cmd);

		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

		submitInfo.pWaitDstStageMask = &waitStage;

		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = &m_FrameData.presentSemaphore;

		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = &m_FrameData.renderSemaphore;

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;

		VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_FrameData.renderFence));

		VkPresentInfoKHR presentInfo = vkinit::present_info();

		presentInfo.pSwapchains = &m_Swapchain;
		presentInfo.swapchainCount = 1;

		presentInfo.pWaitSemaphores = &m_FrameData.renderSemaphore;
		presentInfo.waitSemaphoreCount = 1;

		presentInfo.pImageIndices = &swapchainImageIndex;

		VK_CHECK(vkQueuePresentKHR(m_GraphicsQueue, &presentInfo));

		m_FrameNumber++;
	}

	void VulkanEngine::exec_renderpass(VkRenderPass renderpass, VkFramebuffer framebuffer, uint32_t w, uint32_t h,
		uint32_t attachmentCount, glm::vec4 clearColor, std::function<void()> &&func)
	{
		VkCommandBuffer cmd = m_FrameData.mainCommandBuffer;

		VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(renderpass, { w, h }, framebuffer);

		VkClearValue clearValue{};
		clearValue.color = { {clearColor.r, clearColor.g, clearColor.b, clearColor.a} };
		VkClearValue depthClear{};
		depthClear.depthStencil.depth = 1.0f;

		//VkClearValue clearValues[] = { clearValue, depthClear };
		std::vector<VkClearValue> clearValues;
		clearValues.push_back(clearValue);
		clearValues.push_back(depthClear);
		clearValues.resize(attachmentCount);

		rpInfo.pClearValues = clearValues.data();
		rpInfo.clearValueCount = attachmentCount;

		VkViewport viewport{};
		viewport.x = 0;
		viewport.y = 0;
		viewport.height = (float)h;
		viewport.width = (float)w;
		viewport.minDepth = 0;
		viewport.maxDepth = 1;

		VkRect2D scissor{};
		scissor.offset = { 0, 0 };
		scissor.extent = { w, h };

		vkCmdSetViewport(cmd, 0, 1, &viewport);
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		func();
		vkCmdEndRenderPass(cmd);
	}

	void VulkanEngine::draw() {

		uint32_t swapchainImageIndex;
		prepare_frame(&swapchainImageIndex);

		VkCommandBuffer cmd = m_FrameData.mainCommandBuffer;

		VkExtent2D scaledViewport = { m_ViewportExtent.width * m_RenderResolution,
			m_ViewportExtent.height * m_RenderResolution };

		exec_renderpass(m_ViewportRenderPass, m_ViewportFramebuffer.framebuffer,
			scaledViewport.width, scaledViewport.height,
			2, { 0, 0, 0, 1 }, [&]() {
			draw_objects(cmd, m_RenderObjects.data(), (int)m_RenderObjects.size());
		});

		exec_renderpass(m_ImGuiRenderPass, m_ImGuiFrameBuffers[swapchainImageIndex],
			m_WindowExtent.width, m_WindowExtent.height,
			1, { 0, 0, 0, 1 }, [&]() {

			ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

		ImGui::ShowDemoWindow();

		ImGui::Begin("Texture Viewer");
		Ref<Texture> tex = m_VkManager.get_texture("rgb_test").value();
		ImGui::Image(tex->descriptor, ImVec2(1200, 1200));
		ImGui::End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("Viewport");
		ImGui::PopStyleVar();

		auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
		auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
		auto viewportOffset = ImGui::GetWindowPos();
		glm::vec2 viewportBounds[2];
		viewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
		viewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };
		auto viewportSize = viewportBounds[1] - viewportBounds[0];

		ImGui::Image(m_ViewportFramebuffer.framebufferTexture[0].descriptor, { viewportSize.x, viewportSize.y });
		ImGui::End();

		ImGui::Begin("Settings");

		float resolution = m_RenderResolution;
		ImGui::SliderFloat("Resolution", &resolution, 0.0f, 1.0f);
		if (resolution != m_RenderResolution && resolution != 0) {
			m_ViewportbufferResized = true;
			m_RenderResolution = resolution;
		}

		ImGui::End();

		ImGui::Render();
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

		if (viewportSize.x != m_ViewportExtent.width || viewportSize.y != m_ViewportExtent.height) {
			Atlas::ViewportResizedEvent event = { (uint32_t)viewportSize.x, (uint32_t)viewportSize.y };
			Atlas::Event e(event);
			m_App_Callback(e);
		}

		});

		end_frame(swapchainImageIndex);

		ImGuiIO &io = ImGui::GetIO();

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			GLFWwindow *backup_current_context = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backup_current_context);
		}

	}

	VkBool32
		spdlog_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT msgSeverity,
			VkDebugUtilsMessageTypeFlagsEXT msgType,
			const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
			void *) {
		auto mt = vkb::to_string_message_type(msgType);

		switch (msgSeverity) {
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
			CORE_TRACE("({}) {}", mt, pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
			CORE_INFO("({}) {}", mt, pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
			CORE_WARN("({}) {}", mt, pCallbackData->pMessage);
			break;
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		case VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT:
			CORE_ERROR("({}) {}", mt, pCallbackData->pMessage);
			break;
		}

		return VK_FALSE;
	}

	void VulkanEngine::init_vulkan(Window &window) {
		vkb::Instance vkb_inst = vkb::InstanceBuilder()
			.set_app_name("Vulkan Application")
			.request_validation_layers(true)
			.require_api_version(1, 1, 0)
			.set_debug_callback(spdlog_debug_callback)
			.build()
			.value();

		m_Instance = vkb_inst.instance;
		m_DebugMessenger = vkb_inst.debug_messenger;

		//VK_CHECK(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface));
		VK_CHECK(window.create_window_surface(m_Instance, &m_Surface));

		vkb::PhysicalDevice physicalDevice = vkb::PhysicalDeviceSelector(vkb_inst)
			.set_minimum_version(1, 1)
			.set_surface(m_Surface)
			.select()
			.value();

		VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures{};
		shaderDrawParametersFeatures.sType =
			VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
		shaderDrawParametersFeatures.pNext = nullptr;
		shaderDrawParametersFeatures.shaderDrawParameters = VK_TRUE;
		vkb::Device vkbDevice = vkb::DeviceBuilder(physicalDevice)
			.add_pNext(&shaderDrawParametersFeatures)
			.build()
			.value();

		m_GPUProperties = vkbDevice.physical_device.properties;

		m_Device = vkbDevice.device;
		m_PhysicalDevice = physicalDevice.physical_device;

		m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
		m_GraphicsQueueFamily = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.physicalDevice = m_PhysicalDevice;
		allocatorInfo.device = m_Device;
		allocatorInfo.instance = m_Instance;
		vmaCreateAllocator(&allocatorInfo, &m_Allocator);

		m_VkManager.init(m_Device, m_Allocator);

		m_MainDeletionQueue.push_function([=]() {
			vmaDestroyAllocator(m_Allocator);
		vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
		vkDestroyDevice(m_Device, nullptr);
		vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
		vkDestroyInstance(m_Instance, nullptr);
		});
	}

	void VulkanEngine::init_swapchain() {

		vkb::Swapchain vkbSwapchain =
			vkb::SwapchainBuilder(m_PhysicalDevice, m_Device, m_Surface)
			.set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLORSPACE_SRGB_NONLINEAR_KHR })
			.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
			.set_desired_extent(m_WindowExtent.width, m_WindowExtent.height)
			.build()
			.value();

		m_Swapchain = vkbSwapchain.swapchain;
		m_SwapchainImages = vkbSwapchain.get_images().value();
		m_SwapchainImageViews = vkbSwapchain.get_image_views().value();

		m_SwapchainImageFormat = vkbSwapchain.image_format;
		m_DepthFormat = VK_FORMAT_D32_SFLOAT;
	}

	void VulkanEngine::cleanup_swapchain() {
		vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);

		for (uint32_t i = 0; i < m_SwapchainImageViews.size(); i++) {

			vkDestroyFramebuffer(m_Device, m_ImGuiFrameBuffers[i], nullptr);
			vkDestroyImageView(m_Device, m_SwapchainImageViews[i], nullptr);
		}
	}

	void VulkanEngine::rebuild_swapchain() {

		vkDeviceWaitIdle(m_Device);
		cleanup_swapchain();
		init_swapchain();
		init_framebuffers();
	}

	void VulkanEngine::init_commands() {
		{
			VkCommandPoolCreateInfo cmdPoolInfo = vkinit::command_pool_create_info(
				m_GraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

			VK_CHECK(vkCreateCommandPool(m_Device, &cmdPoolInfo, nullptr,
				&m_FrameData.commandPool));

			VkCommandBufferAllocateInfo cmdAllocInfo =
				vkinit::command_buffer_allocate_info(m_FrameData.commandPool, 1);

			VK_CHECK(vkAllocateCommandBuffers(m_Device, &cmdAllocInfo,
				&m_FrameData.mainCommandBuffer));

			m_MainDeletionQueue.push_function([=]() {
				vkDestroyCommandPool(m_Device, m_FrameData.commandPool, nullptr);
			});
		}

		{
			m_VkManager.init_commands(m_GraphicsQueue, m_GraphicsQueueFamily);
		}
	}

	void VulkanEngine::init_renderpass() {
		{
			VkAttachmentDescription attachment = {};
			attachment.format = m_SwapchainImageFormat;
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference color_attachment = {};
			color_attachment.attachment = 0;
			color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &color_attachment;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0; // or VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			info.attachmentCount = 1;
			info.pAttachments = &attachment;
			info.subpassCount = 1;
			info.pSubpasses = &subpass;
			info.dependencyCount = 1;
			info.pDependencies = &dependency;
			VK_CHECK(vkCreateRenderPass(m_Device, &info, nullptr, &m_ImGuiRenderPass));
		}

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(m_Device, m_ImGuiRenderPass, nullptr);
		});
	}

	uint32_t to_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
		uint32_t result = (a << 24) | (b << 16) | (g << 8) | r;
		return result;
	}

	void print_rgb(uint32_t color) {
		uint8_t a = (color & 0xff000000) >> 24;
		uint8_t b = (color & 0x00ff0000) >> 16;
		uint8_t g = (color & 0x0000ff00) >> 8;
		uint8_t r = color & 0x000000ff;

		CORE_INFO("(r, g, b, a): {}, {}, {}, {}", r, g, b, a);
	}


	void VulkanEngine::init_vp_renderpass() {
		const uint32_t swapchainImageCount = static_cast<uint32_t>(m_SwapchainImages.size());

		{
			VkAttachmentDescription colorAttachment{};
			colorAttachment.format = m_SwapchainImageFormat;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			VkAttachmentReference colorAttachmentRef{};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkAttachmentDescription depthAttachment{};
			depthAttachment.flags = 0;
			depthAttachment.format = m_DepthFormat;
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkAttachmentReference depthAttachmentRef = {};
			depthAttachmentRef.attachment = 1;
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass{};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;
			subpass.pDepthStencilAttachment = &depthAttachmentRef;

			VkSubpassDependency dependency{};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkSubpassDependency depthDependency = {};
			depthDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			depthDependency.dstSubpass = 0;
			depthDependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			depthDependency.srcAccessMask = 0;
			depthDependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			depthDependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

			VkAttachmentDescription attachments[2] = { colorAttachment, depthAttachment };
			VkSubpassDependency dependencies[2] = { dependency, depthDependency };

			VkRenderPassCreateInfo renderPassInfo{};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 2;
			renderPassInfo.pAttachments = &attachments[0];
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = &dependencies[0];

			VK_CHECK(vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_ViewportRenderPass));
		}

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyRenderPass(m_Device, m_ViewportRenderPass, nullptr);
		});

	}

	void VulkanEngine::init_vp_framebuffers() {

		const int w = (int)m_ViewportExtent.width * m_RenderResolution;
		const int h = (int)m_ViewportExtent.height * m_RenderResolution;

		TextureCreateInfo depthInfo = depth_texture_create_info(w, h, m_DepthFormat);
		TextureCreateInfo colorInfo = color_texture_create_info(w, h, m_SwapchainImageFormat);

		colorInfo.filter = VK_FILTER_NEAREST;

		m_ViewportFramebuffer = FramebufferBuilder(w, h, m_ViewportRenderPass)
			.push_attachment(colorInfo)
			.push_attachment(depthInfo).build(m_VkManager);
	}

	void VulkanEngine::rebuild_vp_framebuffer() {
		if (m_ViewportMinimized) return;

		vkDeviceWaitIdle(m_Device);

		destroy_framebuffer(m_VkManager, m_ViewportFramebuffer);
		init_vp_framebuffers();
	}

	void VulkanEngine::init_framebuffers() {
		VkFramebufferCreateInfo imguiFbInfo =
			vkinit::framebuffer_create_info(m_ImGuiRenderPass, m_WindowExtent);

		const uint32_t swapchainImageCount = static_cast<uint32_t>(m_SwapchainImages.size());
		m_ImGuiFrameBuffers = std::vector<VkFramebuffer>(swapchainImageCount);

		// create framebuffers for each of the swapchain image views
		for (uint32_t i = 0; i < swapchainImageCount; i++) {
			VkImageView attachments = m_SwapchainImageViews[i];

			imguiFbInfo.pAttachments = &attachments;
			imguiFbInfo.attachmentCount = 1;

			VK_CHECK(vkCreateFramebuffer(m_Device, &imguiFbInfo, nullptr,
				&m_ImGuiFrameBuffers[i]));

		}
	}

	void VulkanEngine::init_sync_structures() {

		m_VkManager.init_sync_structures();

		VkFenceCreateInfo fenceCreateInfo =
			vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);

		VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();

		VK_CHECK(vkCreateFence(m_Device, &fenceCreateInfo, nullptr,
			&m_FrameData.renderFence));

		m_MainDeletionQueue.push_function(
			[=]() { vkDestroyFence(m_Device, m_FrameData.renderFence, nullptr); });

		VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr,
			&m_FrameData.presentSemaphore));
		VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr,
			&m_FrameData.renderSemaphore));

		m_MainDeletionQueue.push_function([=]() {
			vkDestroySemaphore(m_Device, m_FrameData.presentSemaphore, nullptr);
		vkDestroySemaphore(m_Device, m_FrameData.renderSemaphore, nullptr);
		});
	}

	void VulkanEngine::init_pipelines() {

		VkPipelineLayout layout{};

		VkPipeline meshPipeline{};
		VkPipeline debugPipeline{};

		VertexInputDescription vertexDescription = VertexInputDescriptionBuilder()
			.push_attrib(VertexAttributeType::FLOAT3, &Vertex::position)
			.push_attrib(VertexAttributeType::FLOAT3, &Vertex::normal)
			.push_attrib(VertexAttributeType::FLOAT3, &Vertex::color)
			.push_attrib(VertexAttributeType::FLOAT3, &Vertex::uv)
			.value();

		{
			VkShaderModule vertShader;
			VkShaderModule fragShader;
			load_spirv_shader_module("res/shaders/textured_lit.vert.spv", &vertShader, m_Device);
			load_spirv_shader_module("res/shaders/textured_lit.frag.spv", &fragShader, m_Device);

			VkPipelineLayoutCreateInfo meshPipelineLayoutInfo =
				vkinit::pipeline_layout_create_info();

			PipelineBuilder(m_VkManager)
				.set_renderpass(m_ViewportRenderPass)
				.set_vertex_description(vertexDescription)
				.set_descriptor_layouts({ m_CameraSetLayout, m_ObjectSetLayout, m_SingleTextureSetLayout })
				.add_shader_module(vertShader, VK_SHADER_STAGE_VERTEX_BIT)
				.add_shader_module(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)
				.set_depth_stencil(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
				.build(&meshPipeline, &layout);

			m_VkManager.create_material("textured_mat", meshPipeline, layout);

			vkDestroyShaderModule(m_Device, fragShader, nullptr);
			vkDestroyShaderModule(m_Device, vertShader, nullptr);
		}

		{
			VkShaderModule vertShader;
			VkShaderModule fragShader;
			load_spirv_shader_module("res/shaders/debug.vert.spv", &vertShader, m_Device);
			load_spirv_shader_module("res/shaders/debug.frag.spv", &fragShader, m_Device);

			PipelineBuilder(m_VkManager)
				.set_renderpass(m_ViewportRenderPass)
				.set_vertex_description(vertexDescription)
				.set_descriptor_layouts({ m_CameraSetLayout, m_ObjectSetLayout, m_SingleTextureSetLayout })
				.add_shader_module(vertShader, VK_SHADER_STAGE_VERTEX_BIT)
				.add_shader_module(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT)
				.set_depth_stencil(true, true, VK_COMPARE_OP_LESS_OR_EQUAL)
				.build(&debugPipeline);


			m_VkManager.create_material("debug_mat", debugPipeline, layout);

			vkDestroyShaderModule(m_Device, fragShader, nullptr);
			vkDestroyShaderModule(m_Device, vertShader, nullptr);
		}

		m_MainDeletionQueue.push_function([=]() {
			vkDestroyPipeline(m_Device, meshPipeline, nullptr);

		vkDestroyPipeline(m_Device, debugPipeline, nullptr);
		});
	}

	void VulkanEngine::load_meshes() {
		Ref<Mesh> empire = load_mesh_from_obj("res/models/lost_empire.obj").value();

		Ref<Mesh> triangle = make_ref<Mesh>();
		triangle->vertices.resize(3);
		triangle->vertices[0].position = { 0.5f, 0.5f, 0.0f };
		triangle->vertices[1].position = { -0.5f, 0.5f, 0.0f };
		triangle->vertices[2].position = { 0.f, -0.5f, 0.0f };

		triangle->vertices[0].color = { 1.f, 0.f, 0.0f };
		triangle->vertices[1].color = { 0.f, 1.f, 0.0f };
		triangle->vertices[2].color = { 0.f, 0.f, 1.0f };

		upload_mesh(empire);
		upload_mesh(triangle);

		m_VkManager.set_mesh("empire", empire);
		m_VkManager.set_mesh("triangle", triangle);
	}

	void VulkanEngine::upload_mesh(Ref<Mesh> mesh) {

		m_VkManager.upload_to_gpu(mesh->vertices.data(), (uint32_t)mesh->vertices.size() * sizeof(Vertex),
			mesh->vertexBuffer,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT);

		m_MainDeletionQueue.push_function([=]() {
			vmaDestroyBuffer(m_Allocator, mesh->vertexBuffer.buffer,
			mesh->vertexBuffer.allocation);
		});
	}

	void VulkanEngine::resize_window(uint32_t w, uint32_t h)
	{
		if (m_WindowExtent.width == w && m_WindowExtent.height == h) return;
		m_FramebufferResized = true;
		m_WindowExtent.width = w;
		m_WindowExtent.height = h;

		if (w == 0 || h == 0) m_WindowMinimized = true;
		else m_WindowMinimized = false;
	}

	void VulkanEngine::resize_viewport(uint32_t w, uint32_t h)
	{
		if (m_ViewportExtent.width == w && m_ViewportExtent.height == h) return;
		m_ViewportbufferResized = true;
		m_ViewportExtent.width = w;
		m_ViewportExtent.height = h;

		if (w == 0 || h == 0) m_ViewportMinimized = true;
		else m_ViewportMinimized = false;
	}

	void VulkanEngine::init_scene() {
		RenderObject empire;
		empire.mesh = m_VkManager.get_mesh("empire").value();
		empire.material = m_VkManager.get_material("textured_mat").value();
		empire.transformMatrix = glm::translate(glm::vec3{ 5, -10, 0 });

		m_RenderObjects.push_back(empire);

		Ref<Material> texturedMat = m_VkManager.get_material("textured_mat").value();

		Ref<Texture> texture = m_VkManager.get_texture("empire_diffuse").value();

		DescriptorBuilder(m_VkManager)
			.bind_image(0, *texture.get(), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT)
			.build(&texturedMat->textureSet);

		for (int x = -20; x <= 20; x++) {
			for (int y = -20; y <= 20; y++) {

				RenderObject tri;
				tri.mesh = m_VkManager.get_mesh("triangle").value();
				tri.material = m_VkManager.get_material("debug_mat").value();
				glm::mat4 translation = glm::translate(glm::mat4{ 1.0 }, glm::vec3(x, 3, y));
				glm::mat4 scale = glm::scale(glm::mat4{ 1.0 }, glm::vec3(0.2, 0.2, 0.2));
				tri.transformMatrix = translation * scale;

				m_RenderObjects.push_back(tri);
			}
		}

	}

	void VulkanEngine::init_descriptors() {

		{
			VkDescriptorSetLayoutBinding textureBind =
				vkinit::descriptorset_layout_binding(
					VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
					VK_SHADER_STAGE_FRAGMENT_BIT, 0);

			VkDescriptorSetLayoutCreateInfo setInfo{};
			setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			setInfo.pNext = nullptr;
			setInfo.bindingCount = 1;
			setInfo.flags = 0;
			setInfo.pBindings = &textureBind;

			m_SingleTextureSetLayout =
				m_VkManager.get_descriptor_layout_cache().create_descriptor_layout(setInfo);
		}

		// const size_t sceneParamBufferSize =
		// pad_uniform_buffer_size(sizeof(GPUSceneData));
		const int MAX_OBJECTS = 10000; // TODO: dynamic object buffer?

		create_buffer(m_VkManager, sizeof(GPUObjectData) * MAX_OBJECTS,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&m_FrameData.objectBuffer);

		create_buffer(m_VkManager, sizeof(GPUCameraData),
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			&m_FrameData.cameraBuffer
		);

		DescriptorBuilder(m_VkManager)
			.bind_buffer(0, m_FrameData.cameraBuffer, sizeof(GPUCameraData),
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(&m_FrameData.cameraDescriptor, &m_CameraSetLayout);

		DescriptorBuilder(m_VkManager)
			.bind_buffer(0, m_FrameData.objectBuffer, sizeof(GPUObjectData) * MAX_OBJECTS,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
			.build(&m_FrameData.objectDescriptor, &m_ObjectSetLayout);

		m_MainDeletionQueue.push_function([&]() {

			destroy_buffer(m_VkManager, m_FrameData.objectBuffer);
		destroy_buffer(m_VkManager, m_FrameData.cameraBuffer);
		});
	}

	void VulkanEngine::init_imgui(Window &window) {
		// 1: create descriptor pool for IMGUI
		//  the size of the pool is very oversize, but it's copied from imgui demo
		//  itself.
		VkDescriptorPoolSize pool_sizes[] = {
			{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
			{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
			{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
			{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
			{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
			{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
			{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000} };

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = (uint32_t)std::size(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;

		VkDescriptorPool imguiPool;
		VK_CHECK(vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &imguiPool));

		// 2: initialize imgui library

		// this initializes the core structures of imgui
		ImGui::CreateContext();

		ImGuiIO &io = ImGui::GetIO();
		(void)io;
		// io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable
		// Keyboard Controls io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; //
		// Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport /
		// Platform Windows
	// io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
	// io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

		io.FontGlobalScale = 1.0f;

		ImGui::StyleColorsDark();

		ImGuiStyle &style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}

		ImGui_ImplGlfw_InitForVulkan(window.get_native_window(), true);

		// this initializes imgui for Vulkan
		{
			ImGui_ImplVulkan_InitInfo initInfo = {};
			initInfo.Instance = m_Instance;
			initInfo.PhysicalDevice = m_PhysicalDevice;
			initInfo.Device = m_Device;
			initInfo.Queue = m_GraphicsQueue;
			initInfo.DescriptorPool = imguiPool;
			initInfo.MinImageCount = 3;
			initInfo.ImageCount = 3;
			initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
			ImGui_ImplVulkan_Init(&initInfo, m_ImGuiRenderPass);
		}

		{
			ImFontConfig fontConfig;
			fontConfig.FontDataOwnedByAtlas = false;
			ImFont *robotoFont = io.Fonts->AddFontFromMemoryTTF(
				(void *)c_RobotoRegular, sizeof(c_RobotoRegular), 23.0f, &fontConfig);
			io.FontDefault = robotoFont;

			m_VkManager.immediate_submit(
				[&](VkCommandBuffer cmd) { ImGui_ImplVulkan_CreateFontsTexture(cmd); });
		}

		// clear font textures from cpu data
		ImGui_ImplVulkan_DestroyFontUploadObjects();

		ImGui::SetOneDarkTheme();

		// add the destroy the imgui created structures
		m_MainDeletionQueue.push_function([=]() {
			vkDestroyDescriptorPool(m_Device, imguiPool, nullptr);
		ImGui_ImplVulkan_Shutdown();
		});
	}

	size_t VulkanEngine::pad_uniform_buffer_size(size_t originalSize) {
		size_t minUboAlignment =
			m_GPUProperties.limits.minUniformBufferOffsetAlignment;
		size_t alignedSize = originalSize;
		if (minUboAlignment > 0) {
			alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
		}
		return alignedSize;
	}

	void VulkanEngine::load_images() {
		{
			auto info = vkinit::sampler_create_info(VK_FILTER_NEAREST);
			Ref<Texture> texture =
				load_texture("res/images/lost_empire-RGBA.png", m_VkManager, info).value();

			m_VkManager.set_texture("empire_diffuse", texture);

			m_MainDeletionQueue.push_function([=] {
				destroy_texture(m_VkManager, *texture);
			});
		}

		{
			auto info = vkinit::sampler_create_info(VK_FILTER_NEAREST);
			Ref<Texture> texture =
				load_texture("res/images/rgb_test.png", m_VkManager, info).value();

			m_VkManager.set_texture("rgb_test", texture);

			m_MainDeletionQueue.push_function([=] {
				destroy_texture(m_VkManager, *texture);
			});
		}
	}

}
