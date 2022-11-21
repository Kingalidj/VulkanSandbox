#include "vk_shader.h"

#include <shaderc/shaderc.hpp>

#include <spirv_cross/spirv_reflect.hpp>
#include <spirv_cross/spirv_cross.hpp>

namespace vkutil {

	void reflect(std::vector<uint32_t> buffer) {

		spirv_cross::Compiler compiler(buffer);

		spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		CORE_TRACE("{0} uniform buffers", resources.uniform_buffers.size());
		CORE_TRACE("{0} resources", resources.sampled_images.size());

		CORE_TRACE("Uniform buffers:");
		for (const auto &resource : resources.uniform_buffers) {
			const auto &bufferType = compiler.get_type(resource.base_type_id);
			uint32_t bufferSize = compiler.get_declared_struct_size(bufferType);
			uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
			int memberCount = bufferType.member_types.size();

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

		if (!compile_shader_module(buffer.data(), buffer.size() * sizeof(uint32_t),
			outShaderModule, device)) {
			CORE_WARN("Could not compile shader: {}", filePath);
			return false;
		}

		return true;
	}

	bool load_glsl_shader_module(std::filesystem::path filePath,
		VkShaderStageFlagBits type,
		VkShaderModule *outShaderModule,
		const VkDevice device) {

		shaderc_shader_kind kind{};

		switch (type) {
		case VK_SHADER_STAGE_VERTEX_BIT:
			kind = shaderc_vertex_shader;
			break;
		case VK_SHADER_STAGE_FRAGMENT_BIT:
			kind = shaderc_fragment_shader;
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


		shaderc::CompileOptions cOptions;
		cOptions.SetTargetEnvironment(shaderc_target_env_vulkan,
			shaderc_env_version_vulkan_1_1);
		const bool optimize = false;

		if (optimize)
			cOptions.SetOptimizationLevel(shaderc_optimization_level_performance);

		shaderc::Compiler compiler;

		shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
			source, kind, (char *)filePath.c_str(), cOptions);
		if (module.GetCompilationStatus() != shaderc_compilation_status_success) {
			std::cerr << module.GetErrorMessage() << std::endl;
			return false;
		}

		std::vector<uint32_t> buffer =
			std::vector<uint32_t>(module.cbegin(), module.cend());

		if (!compile_shader_module(buffer.data(), buffer.size() * sizeof(uint32_t),

			outShaderModule, device)) {
			CORE_WARN("Could not compile shader: {}", filePath);
			return false;
		}

		return true;
	}

	bool load_glsl_shader_module(std::filesystem::path filePath,
		VkShaderModule *outShaderModule,
		const VkDevice device) {
		auto ext = filePath.extension();

		VkShaderStageFlagBits type;
		if (ext == ".vert") {
			type = VK_SHADER_STAGE_VERTEX_BIT;
		}
		else if (ext == ".frag") {
			type = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		else {
			CORE_WARN("unknown extension for file: {}", filePath);
			return false;
		}

		return load_glsl_shader_module(filePath, type, outShaderModule, device);
	}

} // namespace vkutil
