#include "vk_pipeline.h"
#include "vk_manager.h"

#include <shaderc/shaderc.hpp>

#include <spirv_cross/spirv_reflect.hpp>
#include <spirv_cross/spirv_cross.hpp>

class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface
{
	shaderc_include_result *GetInclude(
		const char *requested_source,
		shaderc_include_type type,
		const char *requesting_source,
		size_t include_depth)
	{
		const std::string name = requested_source;

		std::ifstream is(name);
		std::stringstream buffer;
		buffer << is.rdbuf();

		std::string contents = buffer.str();

		auto container = new std::array<std::string, 2>;
		(*container)[0] = name;
		(*container)[1] = contents;

		auto data = new shaderc_include_result;

		data->user_data = container;

		data->source_name = (*container)[0].data();
		data->source_name_length = (*container)[0].size();

		data->content = (*container)[1].data();
		data->content_length = (*container)[1].size();

		return data;
	};

	void ReleaseInclude(shaderc_include_result *data) override
	{
		delete static_cast<std::array<std::string, 2> *>(data->user_data);
		delete data;
	};
};


namespace vkutil {

	VertexInputDescriptionBuilder::VertexInputDescriptionBuilder(uint32_t size)
	{
		VkVertexInputBindingDescription mainBinding{};
		mainBinding.binding = 0;
		mainBinding.stride = size;
		mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		m_Description.bindings.push_back(mainBinding);
	}

	VertexInputDescriptionBuilder &VertexInputDescriptionBuilder::push_attrib(VertexAttributeType type, uint32_t offset) {

		VkVertexInputAttributeDescription attribute{};
		attribute.binding = 0;
		attribute.location = m_AttribLocation++;
		attribute.format = (VkFormat)type;
		attribute.offset = offset;

		m_Description.attributes.push_back(attribute);
		return *this;
	}


	VkPipelineLayout PipelineLayoutCache::create_pipeline_layout(VkPipelineLayoutCreateInfo &info)
	{
		CORE_ASSERT(m_Device, "PipelineLayoutCache is not initialized");

		PipelineLayoutInfo layoutInfo;

		for (uint32_t i = 0; i < info.setLayoutCount; i++) {
			layoutInfo.m_Layouts.push_back(info.pSetLayouts[i]);
		}

		auto it = m_LayoutCache.find(layoutInfo);

		if (it != m_LayoutCache.end()) return (*it).second;
		else {
			VkPipelineLayout layout;
			vkCreatePipelineLayout(m_Device, &info, nullptr, &layout);

			m_LayoutCache[layoutInfo] = layout;
			return layout;
		}
	}

	void PipelineLayoutCache::cleanup()
	{
		for (auto &pair : m_LayoutCache) {
			vkDestroyPipelineLayout(m_Device, pair.second, nullptr);
		}
	}

	bool PipelineLayoutCache::PipelineLayoutInfo::operator==(const PipelineLayoutInfo &other) const {

		if (other.m_Layouts.size() != m_Layouts.size()) return false;

		for (uint32_t i = 0; i < m_Layouts.size(); i++) {
			if (other.m_Layouts[i] != m_Layouts[i]) return false;
		}

		return true;
	}

	size_t PipelineLayoutCache::PipelineLayoutInfo::hash() const {
		using std::size_t;
		using std::hash;

		size_t result = hash<size_t>()(m_Layouts.size());

		for (const VkDescriptorSetLayout &l : m_Layouts) {
			result ^= hash<size_t>()((uint64_t)l);
		}

		return result;
	}

	PipelineBuilder::PipelineBuilder(VulkanManager &manager)
	{
		m_Device = manager.device();
		m_LayoutCache = &manager.get_pipeline_layout_cache();
		m_VertexInputInfo = vkinit::vertex_input_state_create_info();
	}

	PipelineBuilder &PipelineBuilder::set_color_format(VkFormat format)
	{
		m_ColorFormat = format;
		return *this;
	}

	PipelineBuilder &PipelineBuilder::add_shader_module(VkShaderModule shaderModule, VkShaderStageFlagBits shaderType)
	{
		m_ShaderStageInfo.push_back(vkinit::pipeline_shader_stage_create_info(shaderType, shaderModule));
		return *this;
	}

	PipelineBuilder &PipelineBuilder::set_renderpass(VkRenderPass renderpass)
	{
		m_RenderPass = renderpass;
		return *this;
	}

	PipelineBuilder &PipelineBuilder::set_vertex_description(
		std::vector<VkVertexInputAttributeDescription> &attributes,
		std::vector<VkVertexInputBindingDescription> &bindings) {
		m_VertexInputInfo.pVertexAttributeDescriptions = attributes.data();
		m_VertexInputInfo.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
		m_VertexInputInfo.pVertexBindingDescriptions = bindings.data();
		m_VertexInputInfo.vertexBindingDescriptionCount = (uint32_t)bindings.size();
		return *this;
	}

	PipelineBuilder &PipelineBuilder::set_vertex_description(VertexInputDescription &desc)
	{
		return set_vertex_description(desc.attributes, desc.bindings);
	}

	PipelineBuilder &PipelineBuilder::set_vertex_description(
		VkVertexInputAttributeDescription *pAttributes, uint32_t attributesCount,
		VkVertexInputBindingDescription *pBindings, uint32_t bindingCount)
	{
		m_VertexInputInfo.pVertexAttributeDescriptions = pAttributes;
		m_VertexInputInfo.vertexAttributeDescriptionCount = attributesCount;
		m_VertexInputInfo.pVertexBindingDescriptions = pBindings;
		m_VertexInputInfo.vertexBindingDescriptionCount = bindingCount;
		return *this;
	}

	PipelineBuilder &PipelineBuilder::set_depth_stencil(bool depthTest, bool depthWrite, VkCompareOp compareOp, VkFormat depthFormat)
	{
		m_EnableDepthStencil = true;
		m_DepthStencil = vkinit::depth_stencil_create_info(depthTest, depthWrite, compareOp);
		m_DepthFormat = depthFormat;
		return *this;
	}

	PipelineBuilder &PipelineBuilder::set_descriptor_layouts(std::vector<VkDescriptorSetLayout> layouts)
	{
		//m_DescriptorSetLayout = std::vector<VkDescriptorSetLayout>(layouts);
		m_DescriptorSetLayout = layouts;
		return *this;
	}

	bool PipelineBuilder::build(VkPipeline *pipeline, VkPipelineLayout *pipelineLayout)
	{

		VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
		layoutInfo.pSetLayouts = m_DescriptorSetLayout.data();
		layoutInfo.setLayoutCount = (uint32_t)m_DescriptorSetLayout.size();

		VkPipelineLayout layout = m_LayoutCache->create_pipeline_layout(layoutInfo);

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.pNext = nullptr;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		auto colorBlendAttachment = vkinit::color_blend_attachment_state();
		auto inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
		auto rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
		auto multisampling = vkinit::multisampling_state_create_info();

		VkPipelineColorBlendStateCreateInfo colorBlending{};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.pNext = nullptr;

		colorBlending.logicOpEnable = VK_FALSE;
		//colorBlending.logicOp = VK_LOGIC_OP_COPY;
		colorBlending.attachmentCount = 1;
		colorBlending.pAttachments = &colorBlendAttachment;

		VkFormat color_rendering_format = m_ColorFormat;

		VkPipelineRenderingCreateInfoKHR pipeline_create{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
		pipeline_create.pNext = VK_NULL_HANDLE;
		pipeline_create.colorAttachmentCount = 1;
		pipeline_create.pColorAttachmentFormats = &color_rendering_format;
		pipeline_create.depthAttachmentFormat = m_DepthFormat;
		//pipeline_create.stencilAttachmentFormat = 0;

		VkGraphicsPipelineCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		createInfo.pNext = &pipeline_create;

		createInfo.stageCount = (uint32_t)m_ShaderStageInfo.size();
		createInfo.pStages = m_ShaderStageInfo.data();
		createInfo.pVertexInputState = &m_VertexInputInfo;
		createInfo.pInputAssemblyState = &inputAssembly;
		createInfo.pViewportState = &viewportState;
		createInfo.pRasterizationState = &rasterizer;
		createInfo.pMultisampleState = &multisampling;
		createInfo.pColorBlendState = &colorBlending;
		createInfo.layout = layout;
		createInfo.renderPass = m_RenderPass;
		createInfo.subpass = 0;
		createInfo.basePipelineHandle = VK_NULL_HANDLE;

		if (m_EnableDepthStencil)
			createInfo.pDepthStencilState = &m_DepthStencil;

		VkPipelineDynamicStateCreateInfo dynStateInfo{};
		VkDynamicState dynStates[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		dynStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynStateInfo.pNext = nullptr;
		dynStateInfo.pDynamicStates = dynStates;
		dynStateInfo.dynamicStateCount = 2;

		createInfo.pDynamicState = &dynStateInfo;

		*pipelineLayout = layout;
		auto res = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &createInfo, nullptr, pipeline);

		return res == VK_SUCCESS;

	}

	bool PipelineBuilder::build(VkPipeline *pipeline)
	{
		VkPipelineLayout layout;
		return build(pipeline, &layout);
	}

	bool PipelineBuilder::build(Shader *shader)
	{
		return build(&shader->pipeline, &shader->layout);
	}

	void reflect(std::vector<uint32_t> buffer) {

		spirv_cross::Compiler compiler(buffer);

		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		CORE_TRACE("{0} uniform buffers", resources.uniform_buffers.size());
		CORE_TRACE("{0} resources", resources.sampled_images.size());

		CORE_TRACE("Uniform buffers:");
		for (const auto &resource : resources.uniform_buffers) {
			const auto &bufferType = compiler.get_type(resource.base_type_id);
			uint32_t bufferSize = (uint32_t)compiler.get_declared_struct_size(bufferType);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			uint32_t memberCount = (uint32_t)bufferType.member_types.size();

			CORE_TRACE("  {0}", resource.name);
			CORE_TRACE("    Size = {0}", bufferSize);
			CORE_TRACE("    Binding = {0}", binding);
			CORE_TRACE("    Members = {0}", memberCount);
		}


	}

	bool compile_shader_module(uint32_t *buffer, uint32_t byteSize,
		VkShaderModule *outShaderModule,
		const VkDevice device) {
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.pNext = nullptr;

		createInfo.codeSize = byteSize;
		createInfo.pCode = buffer;

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) !=
			VK_SUCCESS) {
			return false;
		}

		*outShaderModule = shaderModule;
		return true;
	}

	bool load_spirv_shader_module(const char *filePath,
		VkShaderModule *outShaderModule,
		const VkDevice device) {
		std::ifstream file(filePath, std::ios::ate | std::ios::binary);
		if (!file.is_open()) {
			CORE_WARN("Could not open spv file: {}", filePath);
			return false;
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

		file.seekg(0);
		file.read((char *)buffer.data(), fileSize);
		file.close();

		if (!compile_shader_module(buffer.data(), (uint32_t)(buffer.size() * sizeof(uint32_t)),
			outShaderModule, device)) {
			CORE_WARN("Could not compile shader: {}", filePath);
			return false;
		}

		return true;
	}

	std::vector<uint32_t> compile_glsl_to_spirv(const std::string &source_name,
		VkShaderStageFlagBits stage, const char *source, size_t sourceSize,
		bool optimize)
	{

		shaderc_shader_kind kind{};

		switch (stage) {
		case VK_SHADER_STAGE_VERTEX_BIT:
			kind = shaderc_vertex_shader;
			break;
		case VK_SHADER_STAGE_FRAGMENT_BIT:
			kind = shaderc_fragment_shader;
			break;
		case VK_SHADER_STAGE_COMPUTE_BIT:
			kind = shaderc_compute_shader;
			break;
		default:
			CORE_WARN("unknown extension for file: {}", source_name);
			return {};
		}

		shaderc::Compiler compiler;
		shaderc::CompileOptions options;

		options.SetIncluder(std::make_unique<ShaderIncluder>());

		options.SetTargetEnvironment(shaderc_target_env_vulkan,
			shaderc_env_version_vulkan_1_1);

		if (optimize) options.SetOptimizationLevel(shaderc_optimization_level_size);

		shaderc::PreprocessedSourceCompilationResult preRes =
			compiler.PreprocessGlsl(source, sourceSize, kind, source_name.c_str(), options);

		if (preRes.GetCompilationStatus() != shaderc_compilation_status_success) {
			CORE_WARN("Preprocess failed for file: {}\n{}", source, preRes.GetErrorMessage());
		}

		std::string prePassedSource(preRes.begin());

		shaderc::SpvCompilationResult module =
			compiler.CompileGlslToSpv(source, sourceSize, kind, source_name.c_str(), options);

		if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
			CORE_WARN(module.GetErrorMessage());
		}

		return { module.cbegin(), module.cend() };
	}

	bool load_glsl_shader_module(const VulkanManager &manager, std::filesystem::path filePath,
		VkShaderStageFlagBits type,
		VkShaderModule *outShaderModule) {

		shaderc_shader_kind kind{};

		switch (type) {
		case VK_SHADER_STAGE_VERTEX_BIT:
			kind = shaderc_vertex_shader;
			break;
		case VK_SHADER_STAGE_FRAGMENT_BIT:
			kind = shaderc_fragment_shader;
			break;
		case VK_SHADER_STAGE_COMPUTE_BIT:
			kind = shaderc_compute_shader;
			break;
		default:
			CORE_WARN("unknown extension for file: {}", filePath);
			return false;
		}

		std::ifstream file(filePath, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			CORE_WARN("Could not open file: {}", filePath);
			return false;
		}

		size_t fileSize = (size_t)file.tellg();
		std::string source;
		source.resize(fileSize / sizeof(char));

		file.seekg(0);
		file.read((char *)source.data(), fileSize);
		file.close();


		std::vector<uint32_t> buffer = compile_glsl_to_spirv(filePath.u8string(), type, source.data(), fileSize);

		if (!compile_shader_module(buffer.data(), (uint32_t)(buffer.size() * sizeof(uint32_t)),
			outShaderModule, manager.device())) {
			CORE_WARN("Could not compile shader: {}", filePath);
			return false;
		}

		return true;
	}

	bool load_glsl_shader_module(const VulkanManager &manager, std::filesystem::path filePath, VkShaderModule *outShaderModule) {
		auto ext = filePath.extension();

		VkShaderStageFlagBits type;
		if (ext == ".vert") {
			type = VK_SHADER_STAGE_VERTEX_BIT;
		}
		else if (ext == ".frag") {
			type = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		else if (ext == ".comp") {
			type = VK_SHADER_STAGE_COMPUTE_BIT;
		}
		else {
			CORE_WARN("unknown extension for file: {}", filePath);
			return false;
		}

		return load_glsl_shader_module(manager, filePath, type, outShaderModule);
	}

	void create_compute_shader(VulkanManager &manager, VkShaderModule module, std::vector<VkDescriptorSetLayout> layouts, VkPipeline *pipeline, VkPipelineLayout *pipelineLayout) {

		VkPipelineLayoutCreateInfo layoutInfo = vkinit::pipeline_layout_create_info();
		layoutInfo.pSetLayouts = layouts.data();
		layoutInfo.setLayoutCount = (uint32_t)layouts.size();

		VkPipelineLayout layout = manager.get_pipeline_layout_cache().create_pipeline_layout(layoutInfo);
		*pipelineLayout = layout;

		VkComputePipelineCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		info.stage.module = module;
		info.stage.pName = "main";
		info.layout = layout;

		VK_CHECK(vkCreateComputePipelines(manager.device(), VK_NULL_HANDLE, 1, &info, nullptr, pipeline));
	}



} //namespace vkutil
