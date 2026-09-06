#include "vk_sharpen.h"
#include "vk_instance.h"
#include "ref_import.h"

#include <string.h>

typedef struct {
	VkDescriptorSetLayout descriptor_set_layout;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSet descriptor_set;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkSampler sampler;
	qboolean ready;
	qboolean active_logged;
} sharpen_state_t;

typedef struct {
	float sharpness;
	uint32_t width;
	uint32_t height;
} sharpen_push_t;

static sharpen_state_t sharpen;

extern unsigned char dlss_sharpen_comp_spv[];
extern int dlss_sharpen_comp_spv_size;

void vk_sharpen_shutdown(void)
{
	if (sharpen.sampler)
		qvkDestroySampler(vk.device, sharpen.sampler, NULL);
	if (sharpen.pipeline)
		qvkDestroyPipeline(vk.device, sharpen.pipeline, NULL);
	if (sharpen.pipeline_layout)
		qvkDestroyPipelineLayout(vk.device, sharpen.pipeline_layout, NULL);
	if (sharpen.descriptor_pool)
		qvkDestroyDescriptorPool(vk.device, sharpen.descriptor_pool, NULL);
	if (sharpen.descriptor_set_layout)
		qvkDestroyDescriptorSetLayout(vk.device,
			sharpen.descriptor_set_layout, NULL);
	memset(&sharpen, 0, sizeof(sharpen));
}

qboolean vk_sharpen_initialize(VkImageView input_view, VkImageView output_view)
{
	VkResult result;
	VkShaderModule shader_module = VK_NULL_HANDLE;
	const char *failed_operation = "initialization";

	vk_sharpen_shutdown();

	VkShaderModuleCreateInfo shader_info;
	memset(&shader_info, 0, sizeof(shader_info));
	shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_info.codeSize = (size_t)dlss_sharpen_comp_spv_size;
	shader_info.pCode = (const uint32_t *)dlss_sharpen_comp_spv;
	result = qvkCreateShaderModule(vk.device, &shader_info, NULL, &shader_module);
	if (result != VK_SUCCESS) {
		failed_operation = "shader module creation";
		goto fail;
	}

	VkDescriptorSetLayoutBinding bindings[2];
	memset(bindings, 0, sizeof(bindings));
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo set_layout_info;
	memset(&set_layout_info, 0, sizeof(set_layout_info));
	set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_layout_info.bindingCount = 2;
	set_layout_info.pBindings = bindings;
	result = qvkCreateDescriptorSetLayout(vk.device, &set_layout_info, NULL,
		&sharpen.descriptor_set_layout);
	if (result != VK_SUCCESS) {
		failed_operation = "descriptor layout creation";
		goto fail;
	}

	VkPushConstantRange push_range;
	memset(&push_range, 0, sizeof(push_range));
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.size = sizeof(sharpen_push_t);
	VkPipelineLayoutCreateInfo pipeline_layout_info;
	memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &sharpen.descriptor_set_layout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_range;
	result = qvkCreatePipelineLayout(vk.device, &pipeline_layout_info, NULL,
		&sharpen.pipeline_layout);
	if (result != VK_SUCCESS) {
		failed_operation = "pipeline layout creation";
		goto fail;
	}

	VkComputePipelineCreateInfo pipeline_info;
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = shader_module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = sharpen.pipeline_layout;
	result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1,
		&pipeline_info, NULL, &sharpen.pipeline);
	if (result != VK_SUCCESS) {
		failed_operation = "compute pipeline creation";
		goto fail;
	}
	qvkDestroyShaderModule(vk.device, shader_module, NULL);
	shader_module = VK_NULL_HANDLE;

	VkDescriptorPoolSize pool_sizes[2];
	memset(pool_sizes, 0, sizeof(pool_sizes));
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[1].descriptorCount = 1;
	VkDescriptorPoolCreateInfo pool_info;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = pool_sizes;
	result = qvkCreateDescriptorPool(vk.device, &pool_info, NULL,
		&sharpen.descriptor_pool);
	if (result != VK_SUCCESS) {
		failed_operation = "descriptor pool creation";
		goto fail;
	}

	VkDescriptorSetAllocateInfo allocate_info;
	memset(&allocate_info, 0, sizeof(allocate_info));
	allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate_info.descriptorPool = sharpen.descriptor_pool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &sharpen.descriptor_set_layout;
	result = qvkAllocateDescriptorSets(vk.device, &allocate_info,
		&sharpen.descriptor_set);
	if (result != VK_SUCCESS) {
		failed_operation = "descriptor allocation";
		goto fail;
	}

	VkSamplerCreateInfo sampler_info;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 0.0f;
	result = qvkCreateSampler(vk.device, &sampler_info, NULL, &sharpen.sampler);
	if (result != VK_SUCCESS) {
		failed_operation = "sampler creation";
		goto fail;
	}

	VkDescriptorImageInfo image_infos[2];
	memset(image_infos, 0, sizeof(image_infos));
	image_infos[0].sampler = sharpen.sampler;
	image_infos[0].imageView = input_view;
	image_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[1].imageView = output_view;
	image_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkWriteDescriptorSet writes[2];
	memset(writes, 0, sizeof(writes));
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = sharpen.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].pImageInfo = &image_infos[0];
	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = sharpen.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &image_infos[1];
	qvkUpdateDescriptorSets(vk.device, 2, writes, 0, NULL);

	sharpen.ready = qtrue;
	ri.Printf(PRINT_ALL, "Vulkan DLSS post-upscale sharpening initialized\n");
	return qtrue;

fail:
	if (shader_module)
		qvkDestroyShaderModule(vk.device, shader_module, NULL);
	ri.Printf(PRINT_WARNING, "Vulkan DLSS sharpening %s failed: %s\n",
		failed_operation, cvtResToStr(result));
	vk_sharpen_shutdown();
	return qfalse;
}

qboolean vk_sharpen_record(VkCommandBuffer command_buffer, float sharpness,
	uint32_t width, uint32_t height)
{
	sharpen_push_t push;
	if (!sharpen.ready || sharpness <= 0.0f || !width || !height)
		return qfalse;
	if (sharpness > 1.0f)
		sharpness = 1.0f;
	push.sharpness = sharpness;
	push.width = width;
	push.height = height;
	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		sharpen.pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		sharpen.pipeline_layout, 0, 1, &sharpen.descriptor_set, 0, NULL);
	qvkCmdPushConstants(command_buffer, sharpen.pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
	qvkCmdDispatch(command_buffer, (width + 7) / 8, (height + 7) / 8, 1);
	if (!sharpen.active_logged) {
		ri.Printf(PRINT_ALL, "Vulkan DLSS sharpening active at %.0f%%\n",
			sharpness * 100.0f);
		sharpen.active_logged = qtrue;
	}
	return qtrue;
}
