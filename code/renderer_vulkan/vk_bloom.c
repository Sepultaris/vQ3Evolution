#include "vk_bloom.h"
#include "vk_instance.h"
#include "ref_import.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

typedef struct {
	VkDescriptorSetLayout inout_layout;
	VkDescriptorSetLayout composite_layout;
	VkDescriptorPool descriptor_pool;
	VkPipelineLayout inout_pipeline_layout;
	VkPipelineLayout composite_pipeline_layout;
	VkPipeline down_pipeline;
	VkPipeline blur_pipeline;
	VkPipeline composite_pipeline;
	VkSampler sampler_nearest;
	VkSampler sampler_linear;
	VkDescriptorSet down_set;
	VkDescriptorSet blur_h2q_set;
	VkDescriptorSet blur_q_set;
	VkDescriptorSet composite_set;
	VkImageView half_view;
	VkImageView qa_view;
	VkImageView qb_view;
	VkImageView output_view;
	qboolean ready;
	qboolean active_logged;
} bloom_state_t;

typedef struct {
	float threshold;
	uint32_t width;
	uint32_t height;
} bloom_down_push_t;

typedef struct {
	uint32_t width;
	uint32_t height;
	uint32_t iwidth;
	uint32_t iheight;
} bloom_blur_push_t;

typedef struct {
	float strength;
	uint32_t width;
	uint32_t height;
	uint32_t debug;
} bloom_composite_push_t;

static bloom_state_t bloom;

static FILE *bloom_log_file;
static int bloom_log_lines;

static void bloom_logf(const char *fmt, ...)
{
	va_list args;
	if (!bloom_log_file)
		bloom_log_file = fopen("pt_bloom.log", "wb");
	if (!bloom_log_file)
		return;
	va_start(args, fmt);
	vfprintf(bloom_log_file, fmt, args);
	va_end(args);
	fputc('\n', bloom_log_file);
	fflush(bloom_log_file);
	if (++bloom_log_lines > 4096) {
		fclose(bloom_log_file);
		bloom_log_file = NULL;
		bloom_log_lines = 0;
	}
}

extern unsigned char post_bloom_down_comp_spv[];
extern int post_bloom_down_comp_spv_size;
extern unsigned char post_bloom_blur_comp_spv[];
extern int post_bloom_blur_comp_spv_size;
extern unsigned char post_bloom_composite_comp_spv[];
extern int post_bloom_composite_comp_spv_size;

static VkShaderModule bloom_shader_module(const unsigned char *spv, int spv_size,
	const char **failed_operation)
{
	VkShaderModuleCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = (size_t)spv_size;
	info.pCode = (const uint32_t *)spv;
	VkShaderModule module;
	if (qvkCreateShaderModule(vk.device, &info, NULL, &module) != VK_SUCCESS) {
		*failed_operation = "shader module creation";
		return VK_NULL_HANDLE;
	}
	return module;
}

static VkPipeline bloom_pipeline(VkShaderModule module,
	VkPipelineLayout layout, const char **failed_operation)
{
	VkComputePipelineCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	info.stage.module = module;
	info.stage.pName = "main";
	info.layout = layout;
	VkPipeline pipeline;
	if (qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &info, NULL,
		&pipeline) != VK_SUCCESS) {
		*failed_operation = "compute pipeline creation";
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

void vk_bloom_shutdown(void)
{
	if (bloom.sampler_linear)
		qvkDestroySampler(vk.device, bloom.sampler_linear, NULL);
	if (bloom.sampler_nearest)
		qvkDestroySampler(vk.device, bloom.sampler_nearest, NULL);
	if (bloom.composite_pipeline)
		qvkDestroyPipeline(vk.device, bloom.composite_pipeline, NULL);
	if (bloom.blur_pipeline)
		qvkDestroyPipeline(vk.device, bloom.blur_pipeline, NULL);
	if (bloom.down_pipeline)
		qvkDestroyPipeline(vk.device, bloom.down_pipeline, NULL);
	if (bloom.composite_pipeline_layout)
		qvkDestroyPipelineLayout(vk.device, bloom.composite_pipeline_layout, NULL);
	if (bloom.inout_pipeline_layout)
		qvkDestroyPipelineLayout(vk.device, bloom.inout_pipeline_layout, NULL);
	if (bloom.descriptor_pool)
		qvkDestroyDescriptorPool(vk.device, bloom.descriptor_pool, NULL);
	if (bloom.composite_layout)
		qvkDestroyDescriptorSetLayout(vk.device, bloom.composite_layout, NULL);
	if (bloom.inout_layout)
		qvkDestroyDescriptorSetLayout(vk.device, bloom.inout_layout, NULL);
	memset(&bloom, 0, sizeof(bloom));
}

qboolean vk_bloom_initialize(VkImageView half_view, VkImageView qa_view,
	VkImageView qb_view, VkImageView output_view, uint32_t width,
	uint32_t height)
{
	const char *failed_operation = "initialization";
	if (!width || !height)
		return qfalse;

	vk_bloom_shutdown();

	bloom.half_view = half_view;
	bloom.qa_view = qa_view;
	bloom.qb_view = qb_view;
	bloom.output_view = output_view;

	VkDescriptorSetLayoutBinding inout_bindings[2];
	memset(inout_bindings, 0, sizeof(inout_bindings));
	inout_bindings[0].binding = 0;
	inout_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	inout_bindings[0].descriptorCount = 1;
	inout_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	inout_bindings[1].binding = 1;
	inout_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	inout_bindings[1].descriptorCount = 1;
	inout_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo inout_info;
	memset(&inout_info, 0, sizeof(inout_info));
	inout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	inout_info.bindingCount = 2;
	inout_info.pBindings = inout_bindings;
	if (qvkCreateDescriptorSetLayout(vk.device, &inout_info, NULL,
		&bloom.inout_layout) != VK_SUCCESS) {
		failed_operation = "descriptor layout creation";
		goto fail;
	}

	VkDescriptorSetLayoutBinding composite_bindings[3];
	memset(composite_bindings, 0, sizeof(composite_bindings));
	composite_bindings[0].binding = 0;
	composite_bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	composite_bindings[0].descriptorCount = 1;
	composite_bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	composite_bindings[1].binding = 1;
	composite_bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	composite_bindings[1].descriptorCount = 1;
	composite_bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	composite_bindings[2].binding = 2;
	composite_bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	composite_bindings[2].descriptorCount = 1;
	composite_bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	VkDescriptorSetLayoutCreateInfo composite_info;
	memset(&composite_info, 0, sizeof(composite_info));
	composite_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	composite_info.bindingCount = 3;
	composite_info.pBindings = composite_bindings;
	if (qvkCreateDescriptorSetLayout(vk.device, &composite_info, NULL,
		&bloom.composite_layout) != VK_SUCCESS) {
		failed_operation = "descriptor layout creation";
		goto fail;
	}

	/* Down and blur share a layout; the range covers both push shapes. */
	VkPushConstantRange inout_range;
	memset(&inout_range, 0, sizeof(inout_range));
	inout_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	inout_range.size = sizeof(bloom_blur_push_t);
	VkPipelineLayoutCreateInfo inout_layout_info;
	memset(&inout_layout_info, 0, sizeof(inout_layout_info));
	inout_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	inout_layout_info.setLayoutCount = 1;
	inout_layout_info.pSetLayouts = &bloom.inout_layout;
	inout_layout_info.pushConstantRangeCount = 1;
	inout_layout_info.pPushConstantRanges = &inout_range;
	if (qvkCreatePipelineLayout(vk.device, &inout_layout_info, NULL,
		&bloom.inout_pipeline_layout) != VK_SUCCESS) {
		failed_operation = "pipeline layout creation";
		goto fail;
	}

	VkPushConstantRange composite_range;
	memset(&composite_range, 0, sizeof(composite_range));
	composite_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	composite_range.size = sizeof(bloom_composite_push_t);
	VkPipelineLayoutCreateInfo composite_layout_info;
	memset(&composite_layout_info, 0, sizeof(composite_layout_info));
	composite_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	composite_layout_info.setLayoutCount = 1;
	composite_layout_info.pSetLayouts = &bloom.composite_layout;
	composite_layout_info.pushConstantRangeCount = 1;
	composite_layout_info.pPushConstantRanges = &composite_range;
	if (qvkCreatePipelineLayout(vk.device, &composite_layout_info, NULL,
		&bloom.composite_pipeline_layout) != VK_SUCCESS) {
		failed_operation = "pipeline layout creation";
		goto fail;
	}

	VkShaderModule module = VK_NULL_HANDLE;
	module = bloom_shader_module(post_bloom_down_comp_spv,
		post_bloom_down_comp_spv_size, &failed_operation);
	if (!module) goto fail;
	bloom.down_pipeline = bloom_pipeline(module, bloom.inout_pipeline_layout,
		&failed_operation);
	qvkDestroyShaderModule(vk.device, module, NULL);
	if (!bloom.down_pipeline) goto fail;

	module = bloom_shader_module(post_bloom_blur_comp_spv,
		post_bloom_blur_comp_spv_size, &failed_operation);
	if (!module) goto fail;
	bloom.blur_pipeline = bloom_pipeline(module, bloom.inout_pipeline_layout,
		&failed_operation);
	qvkDestroyShaderModule(vk.device, module, NULL);
	if (!bloom.blur_pipeline) goto fail;

	module = bloom_shader_module(post_bloom_composite_comp_spv,
		post_bloom_composite_comp_spv_size, &failed_operation);
	if (!module) goto fail;
	bloom.composite_pipeline = bloom_pipeline(module,
		bloom.composite_pipeline_layout, &failed_operation);
	qvkDestroyShaderModule(vk.device, module, NULL);
	if (!bloom.composite_pipeline) goto fail;

	VkDescriptorPoolSize pool_sizes[2];
	memset(pool_sizes, 0, sizeof(pool_sizes));
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[0].descriptorCount = 5;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[1].descriptorCount = 4;
	VkDescriptorPoolCreateInfo pool_info;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 4;
	pool_info.poolSizeCount = 2;
	pool_info.pPoolSizes = pool_sizes;
	if (qvkCreateDescriptorPool(vk.device, &pool_info, NULL,
		&bloom.descriptor_pool) != VK_SUCCESS) {
		failed_operation = "descriptor pool creation";
		goto fail;
	}

	VkDescriptorSetLayout set_layouts[4] = { bloom.inout_layout,
		bloom.inout_layout, bloom.inout_layout, bloom.composite_layout };
	VkDescriptorSet sets[4];
	memset(sets, 0, sizeof(sets));
	VkDescriptorSetAllocateInfo allocate_info;
	memset(&allocate_info, 0, sizeof(allocate_info));
	allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate_info.descriptorPool = bloom.descriptor_pool;
	allocate_info.descriptorSetCount = 4;
	allocate_info.pSetLayouts = set_layouts;
	if (qvkAllocateDescriptorSets(vk.device, &allocate_info, sets) != VK_SUCCESS) {
		failed_operation = "descriptor allocation";
		goto fail;
	}
	bloom.down_set = sets[0];
	bloom.blur_h2q_set = sets[1];
	bloom.blur_q_set = sets[2];
	bloom.composite_set = sets[3];

	VkSamplerCreateInfo sampler_info;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	if (qvkCreateSampler(vk.device, &sampler_info, NULL,
		&bloom.sampler_nearest) != VK_SUCCESS) {
		failed_operation = "sampler creation";
		goto fail;
	}
	sampler_info.magFilter = VK_FILTER_LINEAR;
	sampler_info.minFilter = VK_FILTER_LINEAR;
	if (qvkCreateSampler(vk.device, &sampler_info, NULL,
		&bloom.sampler_linear) != VK_SUCCESS) {
		failed_operation = "sampler creation";
		goto fail;
	}

	VkDescriptorImageInfo image_infos[9];
	VkWriteDescriptorSet writes[8];
	memset(image_infos, 0, sizeof(image_infos));
	memset(writes, 0, sizeof(writes));

	/* down_set: sampling source placeholder (refreshed per frame), +
	 * bright-pass output at half res. */
	image_infos[0].sampler = bloom.sampler_nearest;
	image_infos[0].imageView = half_view;
	image_infos[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[1].imageView = half_view;
	image_infos[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = bloom.down_set;
	writes[0].dstBinding = 0;
	writes[0].descriptorCount = 1;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].pImageInfo = &image_infos[0];
	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = bloom.down_set;
	writes[1].dstBinding = 1;
	writes[1].descriptorCount = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].pImageInfo = &image_infos[1];

	/* blur_h2q_set: half -> quarter. */
	image_infos[2].sampler = bloom.sampler_nearest;
	image_infos[2].imageView = half_view;
	image_infos[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[3].imageView = qa_view;
	image_infos[3].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = bloom.blur_h2q_set;
	writes[2].dstBinding = 0;
	writes[2].descriptorCount = 1;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[2].pImageInfo = &image_infos[2];
	writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[3].dstSet = bloom.blur_h2q_set;
	writes[3].dstBinding = 1;
	writes[3].descriptorCount = 1;
	writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[3].pImageInfo = &image_infos[3];

	/* blur_q_set: quarter -> quarter (ping-pong second blur). */
	image_infos[4].sampler = bloom.sampler_nearest;
	image_infos[4].imageView = qa_view;
	image_infos[4].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[5].imageView = qb_view;
	image_infos[5].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[4].dstSet = bloom.blur_q_set;
	writes[4].dstBinding = 0;
	writes[4].descriptorCount = 1;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[4].pImageInfo = &image_infos[4];
	writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[5].dstSet = bloom.blur_q_set;
	writes[5].dstBinding = 1;
	writes[5].descriptorCount = 1;
	writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[5].pImageInfo = &image_infos[5];

	/* composite_set: source + glowing quarter, output full res. */
	image_infos[6].sampler = bloom.sampler_linear;
	image_infos[6].imageView = half_view;
	image_infos[6].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	image_infos[7].sampler = bloom.sampler_linear;
	image_infos[7].imageView = qb_view;
	image_infos[7].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[6].dstSet = bloom.composite_set;
	writes[6].dstBinding = 0;
	writes[6].descriptorCount = 1;
	writes[6].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[6].pImageInfo = &image_infos[6];
	writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[7].dstSet = bloom.composite_set;
	writes[7].dstBinding = 1;
	writes[7].descriptorCount = 1;
	writes[7].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[7].pImageInfo = &image_infos[7];

	uint32_t write_count = 0;
	VkWriteDescriptorSet out_write;
	memset(&out_write, 0, sizeof(out_write));
	out_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	out_write.dstSet = bloom.composite_set;
	out_write.dstBinding = 2;
	out_write.descriptorCount = 1;
	out_write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	image_infos[8].imageView = output_view;
	image_infos[8].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	out_write.pImageInfo = &image_infos[8];
	qvkUpdateDescriptorSets(vk.device, 8, writes, 0, NULL);
	qvkUpdateDescriptorSets(vk.device, 1, &out_write, 0, NULL);

	bloom.ready = qtrue;
	ri.Printf(PRINT_ALL, "Vulkan post-processing bloom initialized\n");
	bloom_logf("PT_BLOOM initialized: half=%ux%u quarter=%ux%u output=%ux%u",
		(width + 1) / 2, (height + 1) / 2,
		((width + 1) / 2 + 1) / 2, ((height + 1) / 2 + 1) / 2,
		width, height);
	return qtrue;

fail:
	bloom_logf("PT_BLOOM init failed: %s", failed_operation);
	if (bloom.inout_layout)
		vk_bloom_shutdown();
	return qfalse;
}

static void bloom_barrier(VkCommandBuffer command_buffer)
{
	VkMemoryBarrier barrier;
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

qboolean vk_bloom_record(VkCommandBuffer command_buffer, VkImageView src_view,
	uint32_t width, uint32_t height, float strength, float threshold,
	uint32_t debug)
{
	if (!bloom.ready || !src_view || !width || !height || strength <= 0.0f)
		return qfalse;
	bloom_logf("PT_BLOOM record: %ux%u strength=%.2f threshold=%.2f debug=%u",
		width, height, strength, threshold, debug);

	/* Refresh the source bindings: the final frame can come from different
	 * images depending on the upscale path. Single frame in flight, so
	 * updating before dispatch is safe. */
	VkDescriptorImageInfo down_src;
	memset(&down_src, 0, sizeof(down_src));
	down_src.sampler = bloom.sampler_nearest;
	down_src.imageView = src_view;
	down_src.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkWriteDescriptorSet down_write;
	memset(&down_write, 0, sizeof(down_write));
	down_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	down_write.dstSet = bloom.down_set;
	down_write.dstBinding = 0;
	down_write.descriptorCount = 1;
	down_write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	down_write.pImageInfo = &down_src;
	qvkUpdateDescriptorSets(vk.device, 1, &down_write, 0, NULL);

	VkDescriptorImageInfo comp_src;
	memset(&comp_src, 0, sizeof(comp_src));
	comp_src.sampler = bloom.sampler_linear;
	comp_src.imageView = src_view;
	comp_src.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkDescriptorImageInfo comp_glow;
	memset(&comp_glow, 0, sizeof(comp_glow));
	comp_glow.sampler = bloom.sampler_linear;
	comp_glow.imageView = bloom.qb_view;
	comp_glow.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	VkWriteDescriptorSet comp_writes[2];
	memset(comp_writes, 0, sizeof(comp_writes));
	comp_writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	comp_writes[0].dstSet = bloom.composite_set;
	comp_writes[0].dstBinding = 0;
	comp_writes[0].descriptorCount = 1;
	comp_writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	comp_writes[0].pImageInfo = &comp_src;
	comp_writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	comp_writes[1].dstSet = bloom.composite_set;
	comp_writes[1].dstBinding = 1;
	comp_writes[1].descriptorCount = 1;
	comp_writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	comp_writes[1].pImageInfo = &comp_glow;
	qvkUpdateDescriptorSets(vk.device, 2, comp_writes, 0, NULL);

	if (threshold <= 0.0f) threshold = 0.0f;
	if (threshold >= 0.98f) threshold = 0.98f;

	const uint32_t half_w = (width + 1) >> 1;
	const uint32_t half_h = (height + 1) >> 1;
	const uint32_t q_w = (half_w + 1) >> 1;
	const uint32_t q_h = (half_h + 1) >> 1;

	/* 1) Bright-pass downsample: full -> half. */
	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.down_pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.inout_pipeline_layout, 0, 1, &bloom.down_set, 0, NULL);
	bloom_down_push_t down_push = { threshold, width, height };
	qvkCmdPushConstants(command_buffer, bloom.inout_pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(down_push), &down_push);
	qvkCmdDispatch(command_buffer, (half_w + 7) / 8, (half_h + 7) / 8, 1);
	bloom_barrier(command_buffer);

	/* 2) Blur + downsample: half -> quarter. */
	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.blur_pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.inout_pipeline_layout, 0, 1, &bloom.blur_h2q_set, 0, NULL);
	bloom_blur_push_t blur_push = { q_w, q_h, half_w, half_h };
	qvkCmdPushConstants(command_buffer, bloom.inout_pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(blur_push), &blur_push);
	qvkCmdDispatch(command_buffer, (q_w + 7) / 8, (q_h + 7) / 8, 1);
	bloom_barrier(command_buffer);

	/* 3) Second blur at quarter res. */
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.inout_pipeline_layout, 0, 1, &bloom.blur_q_set, 0, NULL);
	bloom_blur_push_t blur_push2 = { q_w, q_h, q_w, q_h };
	qvkCmdPushConstants(command_buffer, bloom.inout_pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(blur_push2), &blur_push2);
	qvkCmdDispatch(command_buffer, (q_w + 7) / 8, (q_h + 7) / 8, 1);
	bloom_barrier(command_buffer);

	/* 4) Upsample + composite to the full-res output. */
	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.composite_pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		bloom.composite_pipeline_layout, 0, 1, &bloom.composite_set, 0, NULL);
	bloom_composite_push_t composite_push = { strength, width, height, debug };
	qvkCmdPushConstants(command_buffer, bloom.composite_pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(composite_push),
		&composite_push);
	qvkCmdDispatch(command_buffer, (width + 7) / 8, (height + 7) / 8, 1);

	if (!bloom.active_logged) {
		ri.Printf(PRINT_ALL, "Vulkan post-processing bloom active (strength=%.2f threshold=%.2f)\n",
			strength, threshold);
		bloom.active_logged = qtrue;
	}
	return qtrue;
}