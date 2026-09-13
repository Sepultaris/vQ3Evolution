#include "vk_nv.h"
#include "vk_instance.h"
#include "ref_import.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct {
	float gain;
	float grain;
	float grainScale;
	float time;
	uint32_t tint;
	uint32_t debug;
	float vignette;
	float pad;
} nv_push_t;

typedef struct {
	VkDescriptorSetLayout descriptor_layout;
	VkDescriptorPool descriptor_pool;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	VkSampler sampler;
	VkDescriptorSet descriptor_set;
	VkImageView output_view;
	uint32_t width;
	uint32_t height;
	qboolean ready;
	qboolean active_logged;
} nv_state_t;

static nv_state_t nv;

static FILE *nv_log_file;
static int nv_log_lines;

static void nv_logf(const char *fmt, ...)
{
	va_list args;
	if (!nv_log_file)
		nv_log_file = fopen("pt_nv.log", "wb");
	if (!nv_log_file)
		return;
	va_start(args, fmt);
	vfprintf(nv_log_file, fmt, args);
	va_end(args);
	fputc('\n', nv_log_file);
	fflush(nv_log_file);
	if (++nv_log_lines > 4096) {
		fclose(nv_log_file);
		nv_log_file = NULL;
		nv_log_lines = 0;
	}
}

extern unsigned char post_NV_comp_spv[];
extern int post_NV_comp_spv_size;

void vk_nv_shutdown(void)
{
	if (nv.sampler)
		qvkDestroySampler(vk.device, nv.sampler, NULL);
	if (nv.pipeline)
		qvkDestroyPipeline(vk.device, nv.pipeline, NULL);
	if (nv.pipeline_layout)
		qvkDestroyPipelineLayout(vk.device, nv.pipeline_layout, NULL);
	if (nv.descriptor_pool)
		qvkDestroyDescriptorPool(vk.device, nv.descriptor_pool, NULL);
	if (nv.descriptor_layout)
		qvkDestroyDescriptorSetLayout(vk.device, nv.descriptor_layout, NULL);
	memset(&nv, 0, sizeof(nv));
}

qboolean vk_nv_initialize(VkImageView output_view, uint32_t width,
	uint32_t height)
{
	const char *failed_operation = "initialization";
	if (!width || !height)
		return qfalse;

	vk_nv_shutdown();

	nv.output_view = output_view;
	nv.width = width;
	nv.height = height;

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
	VkDescriptorSetLayoutCreateInfo layout_info;
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layout_info.bindingCount = 2;
	layout_info.pBindings = bindings;
	if (qvkCreateDescriptorSetLayout(vk.device, &layout_info, NULL,
		&nv.descriptor_layout) != VK_SUCCESS) {
		failed_operation = "descriptor layout creation";
		goto fail;
	}

	VkPushConstantRange push_range;
	memset(&push_range, 0, sizeof(push_range));
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.size = sizeof(nv_push_t);
	VkPipelineLayoutCreateInfo pipeline_layout_info;
	memset(&pipeline_layout_info, 0, sizeof(pipeline_layout_info));
	pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_info.setLayoutCount = 1;
	pipeline_layout_info.pSetLayouts = &nv.descriptor_layout;
	pipeline_layout_info.pushConstantRangeCount = 1;
	pipeline_layout_info.pPushConstantRanges = &push_range;
	if (qvkCreatePipelineLayout(vk.device, &pipeline_layout_info, NULL,
		&nv.pipeline_layout) != VK_SUCCESS) {
		failed_operation = "pipeline layout creation";
		goto fail;
	}

	VkShaderModuleCreateInfo module_info;
	memset(&module_info, 0, sizeof(module_info));
	module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	module_info.codeSize = (size_t)post_NV_comp_spv_size;
	module_info.pCode = (const uint32_t *)post_NV_comp_spv;
	VkShaderModule module;
	if (qvkCreateShaderModule(vk.device, &module_info, NULL, &module) != VK_SUCCESS) {
		failed_operation = "shader module creation";
		goto fail;
	}
	VkComputePipelineCreateInfo pipeline_info;
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = nv.pipeline_layout;
	if (qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_info,
		NULL, &nv.pipeline) != VK_SUCCESS) {
		qvkDestroyShaderModule(vk.device, module, NULL);
		failed_operation = "compute pipeline creation";
		goto fail;
	}
	qvkDestroyShaderModule(vk.device, module, NULL);

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
	if (qvkCreateDescriptorPool(vk.device, &pool_info, NULL,
		&nv.descriptor_pool) != VK_SUCCESS) {
		failed_operation = "descriptor pool creation";
		goto fail;
	}

	VkDescriptorSetAllocateInfo allocate_info;
	memset(&allocate_info, 0, sizeof(allocate_info));
	allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate_info.descriptorPool = nv.descriptor_pool;
	allocate_info.descriptorSetCount = 1;
	allocate_info.pSetLayouts = &nv.descriptor_layout;
	if (qvkAllocateDescriptorSets(vk.device, &allocate_info,
		&nv.descriptor_set) != VK_SUCCESS) {
		failed_operation = "descriptor allocation";
		goto fail;
	}

	VkSamplerCreateInfo sampler_info;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (qvkCreateSampler(vk.device, &sampler_info, NULL,
		&nv.sampler) != VK_SUCCESS) {
		failed_operation = "sampler creation";
		goto fail;
	}

	VkDescriptorImageInfo src_info;
	memset(&src_info, 0, sizeof(src_info));
	src_info.sampler = nv.sampler;
	src_info.imageView = output_view;
	src_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkDescriptorImageInfo out_info;
	memset(&out_info, 0, sizeof(out_info));
	out_info.imageView = output_view;
	out_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkWriteDescriptorSet writes[2];
	memset(writes, 0, sizeof(writes));
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = nv.descriptor_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].pImageInfo = &src_info;
	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = nv.descriptor_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &out_info;
	qvkUpdateDescriptorSets(vk.device, 2, writes, 0, NULL);

	nv.ready = qtrue;
	ri.Printf(PRINT_ALL, "Vulkan post-processing night vision initialized\n");
	nv_logf("PT_NV initialized: output=%ux%u",
		width, height);
	return qtrue;

fail:
	nv_logf("PT_NV init failed: %s", failed_operation);
	if (nv.descriptor_layout)
		vk_nv_shutdown();
	return qfalse;
}

qboolean vk_nv_ready(void) { return nv.ready; }

qboolean vk_nv_record(VkCommandBuffer command_buffer, VkImageView src_view,
	uint32_t width, uint32_t height, float gain, float grain, float time,
	uint32_t tint, uint32_t debug, float vignette)
{
	if (!nv.ready || !src_view || !width || !height)
		return qfalse;
	nv_logf("PT_NV record: %ux%u gain=%.2f grain=%.2f time=%.3f tint=%u debug=%u vignette=%.2f",
		width, height, gain, grain, time, tint, debug, vignette);

	/* Refresh the source binding: the NV pass runs on whatever image the
	 * frame converged on (bloomed or plain source). Single frame in flight,
	 * so updating before dispatch is safe. */
	VkDescriptorImageInfo src_info;
	memset(&src_info, 0, sizeof(src_info));
	src_info.sampler = nv.sampler;
	src_info.imageView = src_view;
	src_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkWriteDescriptorSet src_write;
	memset(&src_write, 0, sizeof(src_write));
	src_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	src_write.dstSet = nv.descriptor_set;
	src_write.dstBinding = 0;
	src_write.descriptorCount = 1;
	src_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	src_write.pImageInfo = &src_info;
	qvkUpdateDescriptorSets(vk.device, 1, &src_write, 0, NULL);

	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		nv.pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		nv.pipeline_layout, 0, 1, &nv.descriptor_set, 0, NULL);
	nv_push_t push;
	memset(&push, 0, sizeof(push));
	push.gain = gain;
	push.grain = grain;
	push.grainScale = 1.25f;
	push.time = time;
	push.tint = tint;
	push.debug = debug;
	push.vignette = vignette;
	qvkCmdPushConstants(command_buffer, nv.pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
	qvkCmdDispatch(command_buffer, (width + 7) / 8, (height + 7) / 8, 1);

	if (!nv.active_logged) {
		ri.Printf(PRINT_ALL, "Vulkan post-processing night vision active (tint=%u brightness=%.2f)\n",
			push.tint, push.gain);
		nv.active_logged = qtrue;
	}
	return qtrue;
}
