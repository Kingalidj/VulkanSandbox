#include "vk_engine.h"

#define GLFW_INCLUDE_VULKAN
#include "GLFW/glfw3.h"

#include "vk_initializers.h"
#include "vk_types.h"

#include "VkBootstrap.h"

#include <shaderc/shaderc.hpp>

#include <cmath>
#include <fstream>
#include <iostream>

#define VK_CHECK(x)                                                            \
  do {                                                                         \
    VkResult err = x;                                                          \
    if (err) {                                                                 \
      std::cerr << "at line: " << __LINE__ << ", in file: " << __FILE__        \
                << "\n";                                                       \
      std::cerr << "Detected Vulkan error: " << err << std::endl;              \
      abort();                                                                 \
    }                                                                          \
  } while (0)

bool load_spirv_shader_module(const char *filePath,
                              VkShaderModule *outShaderModule,
                              const VkDevice device) {
  std::ifstream file(filePath, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

  file.seekg(0);
  file.read((char *)buffer.data(), fileSize);
  file.close();

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.pNext = nullptr;

  createInfo.codeSize = buffer.size() * sizeof(uint32_t);
  createInfo.pCode = buffer.data();

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    return false;
  }

  *outShaderModule = shaderModule;
  return true;
}

bool load_glsl_shader_module(std::filesystem::path filePath,
                             VkShaderModule *outShaderModule,
                             const VkDevice device) {

  shaderc_shader_kind kind;
  auto ext = filePath.extension();

  if (ext == ".vert") {
    kind = shaderc_vertex_shader;
  } else if (ext == ".frag") {
    kind = shaderc_fragment_shader;
  } else {
    std::cerr << "unknown extension for file: " << filePath << std::endl;
    return false;
  }

  std::ifstream file(filePath, std::ios::ate | std::ios::binary);

  if (!file.is_open()) {
    return false;
  }

  size_t fileSize = (size_t)file.tellg();
  /* std::vector<char> buffer(fileSize / sizeof(char)); */
  std::string source;
  source.resize(fileSize / sizeof(char));

  file.seekg(0);
  file.read((char *)source.data(), fileSize);
  file.close();

  shaderc::CompileOptions cOptions;
  cOptions.SetTargetEnvironment(shaderc_target_env_vulkan,
                                shaderc_env_version_vulkan_1_2);
  const bool optimize = true;

  if (optimize)
    cOptions.SetOptimizationLevel(shaderc_optimization_level_performance);

  shaderc::Compiler compiler;

  shaderc::SpvCompilationResult module =
      compiler.CompileGlslToSpv(source.data(), kind, filePath.c_str());
  if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
    std::cerr << module.GetErrorMessage() << std::endl;
    return false;
  }

  std::vector<uint32_t> buffer =
      std::vector<uint32_t>(module.cbegin(), module.cend());

  VkShaderModuleCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.pNext = nullptr;

  createInfo.codeSize = buffer.size() * sizeof(uint32_t);
  createInfo.pCode = buffer.data();

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    return false;
  }

  *outShaderModule = shaderModule;
  return true;
}

VkPipeline PipelineBuilder::build_pipeline(VkDevice device, VkRenderPass pass) {
  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.pNext = nullptr;

  viewportState.viewportCount = 1;
  viewportState.pViewports = &m_Viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &m_Scissor;

  VkPipelineColorBlendStateCreateInfo colorBlending = {};
  colorBlending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlending.pNext = nullptr;

  colorBlending.logicOpEnable = VK_FALSE;
  colorBlending.logicOp = VK_LOGIC_OP_COPY;
  colorBlending.attachmentCount = 1;
  colorBlending.pAttachments = &m_ColorBlendAttachment;

  VkGraphicsPipelineCreateInfo pipelineInfo = {};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineInfo.pNext = nullptr;

  pipelineInfo.stageCount = m_ShaderStages.size();
  pipelineInfo.pStages = m_ShaderStages.data();
  pipelineInfo.pVertexInputState = &m_VertexInputInfo;
  pipelineInfo.pInputAssemblyState = &m_InputAssembly;
  pipelineInfo.pViewportState = &viewportState;
  pipelineInfo.pRasterizationState = &m_Rasterizer;
  pipelineInfo.pMultisampleState = &m_Multisampling;
  pipelineInfo.pColorBlendState = &colorBlending;
  pipelineInfo.layout = m_PipelineLayout;
  pipelineInfo.renderPass = pass;
  pipelineInfo.subpass = 0;
  pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

  VkPipeline pipeLine;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                nullptr, &pipeLine) != VK_SUCCESS) {
    std::cerr << "failed to create pipeline" << std::endl;
    return VK_NULL_HANDLE;
  } else {
    return pipeLine;
  }
}

void VulkanEngine::init() {

  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  m_Window = glfwCreateWindow(m_WindowExtent.width, m_WindowExtent.height,
                              "Vulkan Engine", nullptr, nullptr);

  init_vulkan();
  init_swapchain();
  init_commands();
  init_default_renderpass();
  init_framebuffers();
  init_sync_structures();
  init_pipelines();

  m_IsInitialized = true;
}

void VulkanEngine::cleanup() {
  if (m_IsInitialized) {

    vkDeviceWaitIdle(m_Device);

    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);

    vkDestroyFence(m_Device, m_RenderFence, nullptr);
    vkDestroySemaphore(m_Device, m_RenderSemaphore, nullptr);
    vkDestroySemaphore(m_Device, m_PresentSemaphore, nullptr);

    vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);

    vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);

    for (uint32_t i = 0; i < m_FrameBuffers.size(); i++) {
      vkDestroyFramebuffer(m_Device, m_FrameBuffers[i], nullptr);
      vkDestroyImageView(m_Device, m_SwapChainImageViews[i], nullptr);
    }

    vkDestroyDevice(m_Device, nullptr);
    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    vkb::destroy_debug_utils_messenger(m_Instance, m_DebugMessenger);
    vkDestroyInstance(m_Instance, nullptr);

    glfwDestroyWindow(m_Window);
    glfwTerminate();
  }
}

void VulkanEngine::draw() {
  // wait for last frame to finish. Timeout of 1 second
  VK_CHECK(vkWaitForFences(m_Device, 1, &m_RenderFence, true, 1000000000));
  VK_CHECK(vkResetFences(m_Device, 1, &m_RenderFence));

  // since we waited the buffer is empty
  VK_CHECK(vkResetCommandBuffer(m_MainCommandBuffer, 0));

  // we will write to this image index (framebuffer)
  uint32_t swapchainImageIndex;
  VK_CHECK(vkAcquireNextImageKHR(m_Device, m_SwapChain, 1000000000,
                                 m_PresentSemaphore, nullptr,
                                 &swapchainImageIndex));

  VkCommandBufferBeginInfo cmdBeginInfo = vkinit::command_buffer_begin_info(
      VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

  VK_CHECK(vkBeginCommandBuffer(m_MainCommandBuffer, &cmdBeginInfo));

  VkClearValue clearValue;
  float flash = std::abs(std::sin(m_FrameNumber / 120.f));
  clearValue.color = {{0.0f, 0.0f, flash, 1.0f}};

  // start main renderpass
  VkRenderPassBeginInfo rpInfo = vkinit::renderpass_begin_info(
      m_RenderPass, m_WindowExtent, m_FrameBuffers[swapchainImageIndex]);

  rpInfo.clearValueCount = 1;
  rpInfo.pClearValues = &clearValue;

  vkCmdBeginRenderPass(m_MainCommandBuffer, &rpInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  vkCmdBindPipeline(m_MainCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_TrianglePipeline);
  vkCmdDraw(m_MainCommandBuffer, 3, 1, 0, 0);

  vkCmdEndRenderPass(m_MainCommandBuffer);
  VK_CHECK(vkEndCommandBuffer(m_MainCommandBuffer));

  VkSubmitInfo submitInfo = vkinit::submit_info(&m_MainCommandBuffer);

  VkPipelineStageFlags waitStage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

  submitInfo.pWaitDstStageMask = &waitStage;

  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &m_PresentSemaphore;

  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &m_RenderSemaphore;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_MainCommandBuffer;

  VK_CHECK(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_RenderFence));

  VkPresentInfoKHR presentInfo = vkinit::present_info();

  presentInfo.pSwapchains = &m_SwapChain;
  presentInfo.swapchainCount = 1;

  presentInfo.pWaitSemaphores = &m_RenderSemaphore;
  presentInfo.waitSemaphoreCount = 1;

  presentInfo.pImageIndices = &swapchainImageIndex;

  VK_CHECK(vkQueuePresentKHR(m_GraphicsQueue, &presentInfo));

  m_FrameNumber++;
}

void VulkanEngine::run() {

  while (!glfwWindowShouldClose(m_Window)) {
    glfwPollEvents();

    draw();
  }
}

void VulkanEngine::init_vulkan() {
  vkb::Instance vkb_inst =
      vkb::InstanceBuilder()
          .set_app_name("Vulkan Application")
          .request_validation_layers(true)
          .require_api_version(1, 1, 0)
          .use_default_debug_messenger()
          //          .set_debug_messenger_severity(
          // VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
          .build()
          .value();

  m_Instance = vkb_inst.instance;
  m_DebugMessenger = vkb_inst.debug_messenger;

  VK_CHECK(glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface));

  vkb::PhysicalDevice physicalDevice = vkb::PhysicalDeviceSelector(vkb_inst)
                                           .set_minimum_version(1, 1)
                                           .set_surface(m_Surface)
                                           .select()
                                           .value();

  vkb::Device vkbDevice = vkb::DeviceBuilder(physicalDevice).build().value();

  m_Device = vkbDevice.device;
  m_ChosenGPU = physicalDevice.physical_device;

  m_GraphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
  m_GraphicsQueueFamily =
      vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
}

void VulkanEngine::init_swapchain() {

  vkb::Swapchain vkbSwapchain =
      vkb::SwapchainBuilder(m_ChosenGPU, m_Device, m_Surface)
          .use_default_format_selection()
          .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
          .set_desired_extent(m_WindowExtent.width, m_WindowExtent.height)
          .build()
          .value();

  m_SwapChain = vkbSwapchain.swapchain;
  m_SwapChainImages = vkbSwapchain.get_images().value();
  m_SwapChainImageViews = vkbSwapchain.get_image_views().value();

  m_SwapChainImageFormat = vkbSwapchain.image_format;
}

void VulkanEngine::init_commands() {
  VkCommandPoolCreateInfo cmdPoolInfo = vkinit::command_pool_create_info(
      m_GraphicsQueueFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  VK_CHECK(
      vkCreateCommandPool(m_Device, &cmdPoolInfo, nullptr, &m_CommandPool));

  VkCommandBufferAllocateInfo cmdAllocInfo =
      vkinit::command_buffer_allocate_info(m_CommandPool, 1);
  VK_CHECK(
      vkAllocateCommandBuffers(m_Device, &cmdAllocInfo, &m_MainCommandBuffer));
}

void VulkanEngine::init_default_renderpass() {
  VkAttachmentDescription color_attachment = {};
  color_attachment.format = m_SwapChainImageFormat;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_attachment_ref = {};
  // attachment number will index into the pAttachments array in the
  // parent renderpass
  color_attachment_ref.attachment = 0;
  color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_attachment_ref;

  VkRenderPassCreateInfo render_pass_info = {};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;

  render_pass_info.attachmentCount = 1;
  render_pass_info.pAttachments = &color_attachment;

  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;

  VK_CHECK(
      vkCreateRenderPass(m_Device, &render_pass_info, nullptr, &m_RenderPass));
}

void VulkanEngine::init_framebuffers() {
  VkFramebufferCreateInfo fb_info =
      vkinit::framebuffer_create_info(m_RenderPass, m_WindowExtent);

  const uint32_t swapchain_imagecount = m_SwapChainImages.size();
  m_FrameBuffers = std::vector<VkFramebuffer>(swapchain_imagecount);

  // create framebuffers for each of the swapchain image views
  for (uint32_t i = 0; i < swapchain_imagecount; i++) {
    fb_info.pAttachments = &m_SwapChainImageViews[i];
    VK_CHECK(
        vkCreateFramebuffer(m_Device, &fb_info, nullptr, &m_FrameBuffers[i]));
  }
}

void VulkanEngine::init_sync_structures() {
  VkFenceCreateInfo fenceCreateInfo =
      vkinit::fence_create_info(VK_FENCE_CREATE_SIGNALED_BIT);
  VK_CHECK(vkCreateFence(m_Device, &fenceCreateInfo, nullptr, &m_RenderFence));

  VkSemaphoreCreateInfo semaphoreCreateInfo = vkinit::semaphore_create_info();
  VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr,
                             &m_PresentSemaphore));
  VK_CHECK(vkCreateSemaphore(m_Device, &semaphoreCreateInfo, nullptr,
                             &m_RenderSemaphore));
}

void VulkanEngine::init_pipelines() {

  VkShaderModule triangleFragShader;
  if (!load_glsl_shader_module("res/shaders/triangle.frag", &triangleFragShader,
                               m_Device)) {
    std::cerr << "Could not load triangle.frag.spv" << std::endl;
    return;
  } else {
    std::cout << "Triangle fragment shader successfully loaded" << std::endl;
  }

  VkShaderModule triangleVertexShader;
  if (!load_glsl_shader_module("res/shaders/triangle.vert",
                               &triangleVertexShader, m_Device)) {
    std::cerr << "Could not load triangle.vert.spv" << std::endl;
    return;
  } else {
    std::cout << "Triangle vertex shader successfully loaded" << std::endl;
  }

  VkPipelineLayoutCreateInfo pipeline_layout_info =
      vkinit::pipeline_layout_create_info();

  VK_CHECK(vkCreatePipelineLayout(m_Device, &pipeline_layout_info, nullptr,
                                  &m_TrianglePipelineLayout));

  PipelineBuilder pipelineBuilder;

  pipelineBuilder.m_ShaderStages.push_back(
      vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_VERTEX_BIT,
                                                triangleVertexShader));

  pipelineBuilder.m_ShaderStages.push_back(
      vkinit::pipeline_shader_stage_create_info(VK_SHADER_STAGE_FRAGMENT_BIT,
                                                triangleFragShader));

  pipelineBuilder.m_VertexInputInfo = vkinit::vertex_input_state_create_info();
  pipelineBuilder.m_InputAssembly =
      vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

  pipelineBuilder.m_Viewport.x = 0.0f;
  pipelineBuilder.m_Viewport.y = 0.0f;
  pipelineBuilder.m_Viewport.width = (float)m_WindowExtent.width;
  pipelineBuilder.m_Viewport.height = (float)m_WindowExtent.height;
  pipelineBuilder.m_Viewport.minDepth = 0.0f;
  pipelineBuilder.m_Viewport.maxDepth = 1.0f;

  pipelineBuilder.m_Scissor.offset = {0, 0};
  pipelineBuilder.m_Scissor.extent = m_WindowExtent;

  pipelineBuilder.m_Rasterizer =
      vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
  pipelineBuilder.m_Multisampling = vkinit::multisampling_state_create_info();
  pipelineBuilder.m_ColorBlendAttachment =
      vkinit::color_blend_attachment_state();
  pipelineBuilder.m_PipelineLayout = m_TrianglePipelineLayout;

  m_TrianglePipeline = pipelineBuilder.build_pipeline(m_Device, m_RenderPass);
}
