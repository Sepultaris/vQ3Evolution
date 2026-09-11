#include "tr_local.h"
#include "vk_instance.h"
#include "vk_image.h"
#include "vk_cmd.h"
#include "vk_temporal.h"
#include "vk_streamline.h"
#include "vk_sharpen.h"
#include "vk_bloom.h"
#include "vk_nv.h"
#include "vk_raytracing.h"
#include "vk_pathtrace.h"
#include "tr_cvar.h"
#include "tr_globals.h"

#include <math.h>
#include <string.h>

#if defined(USE_NVIDIA_STREAMLINE) || defined(USE_VULKAN_RAY_TRACING)
#define USE_TEMPORAL_RENDER_TARGETS
#endif

typedef struct {
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkFormat format;
	VkImageUsageFlags usage;
	VkImageAspectFlags aspect;
	VkImageLayout layout;
} temporal_image_t;

typedef struct {
	qboolean active;
	qboolean scene_pass;
	qboolean scene_drawn;
	qboolean ui_pass;
	qboolean have_view;
	qboolean reset;
	uint32_t render_width;
	uint32_t render_height;
	uint32_t output_width;
	uint32_t output_height;
	uint32_t frame_index;
	float jitter_x;
	float jitter_y;
	float projection[16];
	float render_projection[16];
	float view[16];
	float camera_origin[3];
	float camera_axis[9];
	float camera_near;
	float camera_far;
	float camera_fov_y;
	float camera_aspect;
	temporal_image_t scene_color;
	temporal_image_t scene_depth;
	temporal_image_t raytraced_color;
	temporal_image_t neural_color;
	temporal_image_t output_color;
	temporal_image_t sharpened_color;
	temporal_image_t bloom_half;
	temporal_image_t bloom_q_a;
	temporal_image_t bloom_q_b;
	temporal_image_t bloomed_color;
	temporal_image_t nv_color;
	temporal_image_t motion_vectors;
    temporal_image_t path_depth;
	qboolean sharpen_ready;
	qboolean bloom_ready;
	qboolean nv_ready;
	VkRenderPass scene_render_pass;
	VkRenderPass ui_render_pass;
	VkFramebuffer scene_framebuffer;
} temporal_state_t;

static temporal_state_t temporal;

#ifdef USE_TEMPORAL_RENDER_TARGETS

static float halton(uint32_t index, uint32_t base)
{
	float result = 0.0f;
	float fraction = 1.0f;
	while (index) {
		fraction /= (float)base;
		result += fraction * (float)(index % base);
		index /= base;
	}
	return result;
}

static void create_image(temporal_image_t *target, uint32_t width, uint32_t height,
	VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect)
{
	VkImageCreateInfo image_info;
	memset(&image_info, 0, sizeof(image_info));
	image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image_info.imageType = VK_IMAGE_TYPE_2D;
	image_info.format = format;
	image_info.extent.width = width;
	image_info.extent.height = height;
	image_info.extent.depth = 1;
	image_info.mipLevels = 1;
	image_info.arrayLayers = 1;
	image_info.samples = VK_SAMPLE_COUNT_1_BIT;
	image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
	image_info.usage = usage;
	image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(qvkCreateImage(vk.device, &image_info, NULL, &target->image));

	VkMemoryRequirements requirements;
	qvkGetImageMemoryRequirements(vk.device, target->image, &requirements);
	VkMemoryAllocateInfo allocation;
	memset(&allocation, 0, sizeof(allocation));
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(qvkAllocateMemory(vk.device, &allocation, NULL, &target->memory));
	VK_CHECK(qvkBindImageMemory(vk.device, target->image, target->memory, 0));

	VkImageViewCreateInfo view_info;
	memset(&view_info, 0, sizeof(view_info));
	view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	view_info.image = target->image;
	view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
	view_info.format = format;
	view_info.subresourceRange.aspectMask = aspect;
	view_info.subresourceRange.levelCount = 1;
	view_info.subresourceRange.layerCount = 1;
	VK_CHECK(qvkCreateImageView(vk.device, &view_info, NULL, &target->view));
	target->format = format;
	target->usage = usage;
	target->aspect = aspect;
	target->layout = VK_IMAGE_LAYOUT_UNDEFINED;
}

static void destroy_image(temporal_image_t *target)
{
	if (target->view) qvkDestroyImageView(vk.device, target->view, NULL);
	if (target->image) qvkDestroyImage(vk.device, target->image, NULL);
	if (target->memory) qvkFreeMemory(vk.device, target->memory, NULL);
	memset(target, 0, sizeof(*target));
}

static VkRenderPass create_render_pass(VkAttachmentLoadOp color_load,
	VkImageLayout color_initial, VkImageLayout color_final,
	VkAttachmentStoreOp depth_store, VkFormat depth_format)
{
	VkAttachmentDescription attachments[2];
	memset(attachments, 0, sizeof(attachments));
	attachments[0].format = vk.surface_format.format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = color_load;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = color_initial;
	attachments[0].finalLayout = color_final;
	attachments[1].format = depth_format;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = depth_store;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
	VkSubpassDescription subpass;
	memset(&subpass, 0, sizeof(subpass));
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &color_ref;
	subpass.pDepthStencilAttachment = &depth_ref;

	VkRenderPassCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = 2;
	info.pAttachments = attachments;
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	VkRenderPass render_pass;
	VK_CHECK(qvkCreateRenderPass(vk.device, &info, NULL, &render_pass));
	return render_pass;
}

static void initialize_layouts(void)
{
	VkCommandBuffer command_buffer;
	vk_create_command_buffer(vk.command_pool, &command_buffer);
	VkCommandBufferBeginInfo begin;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(qvkBeginCommandBuffer(command_buffer, &begin));

	record_image_layout_transition(command_buffer, temporal.scene_depth.image,
		temporal.scene_depth.aspect, 0,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
	temporal.scene_depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	record_image_layout_transition(command_buffer, temporal.output_color.image,
		VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	temporal.output_color.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	if (temporal.sharpened_color.image) {
		record_image_layout_transition(command_buffer,
			temporal.sharpened_color.image, VK_IMAGE_ASPECT_COLOR_BIT, 0,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.sharpened_color.layout = VK_IMAGE_LAYOUT_GENERAL;
	}
	if (temporal.neural_color.image) {
		record_image_layout_transition(command_buffer, temporal.neural_color.image,
			VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.neural_color.layout = VK_IMAGE_LAYOUT_GENERAL;
	}
	if (temporal.raytraced_color.image) {
		record_image_layout_transition(command_buffer,
			temporal.raytraced_color.image, VK_IMAGE_ASPECT_COLOR_BIT, 0,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_SHADER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.raytraced_color.layout = VK_IMAGE_LAYOUT_GENERAL;
	}
    if (temporal.path_depth.image) {
        record_image_layout_transition(command_buffer, temporal.path_depth.image,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL);
        temporal.path_depth.layout = VK_IMAGE_LAYOUT_GENERAL;
    }
	record_image_layout_transition(command_buffer, temporal.motion_vectors.image,
		VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	PFN_vkCmdClearColorImage clear_color = (PFN_vkCmdClearColorImage)
		qvkGetDeviceProcAddr(vk.device, "vkCmdClearColorImage");
	if (clear_color) {
		VkClearColorValue clear;
		VkImageSubresourceRange range;
		memset(&clear, 0, sizeof(clear));
		memset(&range, 0, sizeof(range));
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.levelCount = 1;
		range.layerCount = 1;
		clear_color(command_buffer, temporal.motion_vectors.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);
	}
	record_image_layout_transition(command_buffer, temporal.motion_vectors.image,
		VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
		VK_IMAGE_LAYOUT_GENERAL);
	temporal.motion_vectors.layout = VK_IMAGE_LAYOUT_GENERAL;

	/* Feature creation may compile NVIDIA pipelines on a cold driver cache.
	 * Record that work during renderer initialization so the first gameplay
	 * frame never has to create the neural-rendering feature lazily. */
	if (temporal.neural_color.image)
		vk_sl_prepare_neural_rendering(command_buffer);

	VK_CHECK(qvkEndCommandBuffer(command_buffer));
	VkSubmitInfo submit;
	memset(&submit, 0, sizeof(submit));
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &command_buffer;
	VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE));
	VK_CHECK(qvkQueueWaitIdle(vk.queue));
	qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &command_buffer);
}

static void begin_pass(VkRenderPass render_pass, VkFramebuffer framebuffer,
	uint32_t width, uint32_t height)
{
	VkClearValue clear[2];
	memset(clear, 0, sizeof(clear));
	clear[0].color.float32[3] = 1.0f;
	clear[1].depthStencil.depth = 1.0f;
	VkRenderPassBeginInfo begin;
	memset(&begin, 0, sizeof(begin));
	begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	begin.renderPass = render_pass;
	begin.framebuffer = framebuffer;
	begin.renderArea.extent.width = width;
	begin.renderArea.extent.height = height;
	begin.clearValueCount = 2;
	begin.pClearValues = clear;
	qvkCmdBeginRenderPass(vk.command_buffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
}

#endif

void vk_temporal_initialize(uint32_t render_width, uint32_t render_height,
	uint32_t output_width, uint32_t output_height)
{
	memset(&temporal, 0, sizeof(temporal));
#ifdef USE_TEMPORAL_RENDER_TARGETS
	temporal.active = ((r_dlss->integer && vk_sl_dlss_supported()) ||
		(r_dlssNeuralRendering->integer && vk_sl_neural_rendering_supported()) ||
		(r_dlssFrameGeneration->integer && vk_sl_frame_generation_supported()) ||
		(r_rayTracing->integer && vk_rt_supported())) ? qtrue : qfalse;
	if (!temporal.active)
		return;
	temporal.render_width = render_width;
	temporal.render_height = render_height;
	temporal.output_width = output_width;
	temporal.output_height = output_height;
	temporal.reset = qtrue;

	const VkImageUsageFlags color_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags depth_usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	const VkImageUsageFlags motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	create_image(&temporal.scene_color, render_width, render_height,
		vk.surface_format.format, color_usage, VK_IMAGE_ASPECT_COLOR_BIT);
	VkFormat temporal_depth_format = vk.fmt_DepthStencil;
	if ((r_dlssNeuralRendering->integer && vk_sl_neural_rendering_supported()) ||
		(r_rayTracing->integer && vk_rt_supported())) {
		VkFormatProperties depth_properties;
		memset(&depth_properties, 0, sizeof(depth_properties));
		qvkGetPhysicalDeviceFormatProperties(vk.physical_device,
			VK_FORMAT_D32_SFLOAT, &depth_properties);
		if ((depth_properties.optimalTilingFeatures &
			(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
			 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) ==
			(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
			 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
			temporal_depth_format = VK_FORMAT_D32_SFLOAT;
	}
	create_image(&temporal.scene_depth, render_width, render_height,
		temporal_depth_format, depth_usage, VK_IMAGE_ASPECT_DEPTH_BIT);
	if (r_rayTracing->integer && vk_rt_supported())
		create_image(&temporal.raytraced_color, render_width, render_height,
			VK_FORMAT_R8G8B8A8_UNORM, color_usage, VK_IMAGE_ASPECT_COLOR_BIT);
	if (r_dlssNeuralRendering->integer && vk_sl_neural_rendering_supported() && !vk_sl_ray_reconstruction_enabled())
		create_image(&temporal.neural_color, render_width, render_height,
			vk.surface_format.format, color_usage, VK_IMAGE_ASPECT_COLOR_BIT);
	create_image(&temporal.output_color, output_width, output_height,
		vk.surface_format.format, color_usage, VK_IMAGE_ASPECT_COLOR_BIT);
	if (r_dlss->integer && vk_sl_dlss_supported()) {
		const VkImageUsageFlags sharpen_usage = VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		create_image(&temporal.sharpened_color, output_width, output_height,
			VK_FORMAT_R8G8B8A8_UNORM, sharpen_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		temporal.sharpen_ready = vk_sharpen_initialize(
			temporal.output_color.view, temporal.sharpened_color.view);
	}
	{
		const VkImageUsageFlags bloom_glow_usage = VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT;
		const VkImageUsageFlags bloom_out_usage = VK_IMAGE_USAGE_STORAGE_BIT |
			VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		const uint32_t bloom_half_w = (output_width + 1) / 2;
		const uint32_t bloom_half_h = (output_height + 1) / 2;
		const uint32_t bloom_quarter_w = (bloom_half_w + 1) / 2;
		const uint32_t bloom_quarter_h = (bloom_half_h + 1) / 2;
		create_image(&temporal.bloom_half, bloom_half_w, bloom_half_h,
			VK_FORMAT_R16G16B16A16_SFLOAT, bloom_glow_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		create_image(&temporal.bloom_q_a, bloom_quarter_w, bloom_quarter_h,
			VK_FORMAT_R16G16B16A16_SFLOAT, bloom_glow_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		create_image(&temporal.bloom_q_b, bloom_quarter_w, bloom_quarter_h,
			VK_FORMAT_R16G16B16A16_SFLOAT, bloom_glow_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		create_image(&temporal.bloomed_color, output_width, output_height,
			VK_FORMAT_R8G8B8A8_UNORM, bloom_out_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		temporal.bloom_ready = vk_bloom_initialize(temporal.bloom_half.view,
			temporal.bloom_q_a.view, temporal.bloom_q_b.view,
			temporal.bloomed_color.view, output_width, output_height);
		create_image(&temporal.nv_color, output_width, output_height,
			VK_FORMAT_R8G8B8A8_UNORM, bloom_out_usage,
			VK_IMAGE_ASPECT_COLOR_BIT);
		temporal.nv_ready = vk_nv_initialize(temporal.nv_color.view,
			output_width, output_height);
	}
	create_image(&temporal.motion_vectors, render_width, render_height,
		VK_FORMAT_R32G32_SFLOAT, motion_usage, VK_IMAGE_ASPECT_COLOR_BIT);
    if (r_rayTracing->integer == 2 && temporal.raytraced_color.image)
        create_image(&temporal.path_depth, render_width, render_height,
            VK_FORMAT_R32_SFLOAT, motion_usage, VK_IMAGE_ASPECT_COLOR_BIT);
	initialize_layouts();
	if (temporal.raytraced_color.image)
		vk_rt_initialize(render_width, render_height,
			temporal.scene_color.view, temporal.scene_color.format,
			temporal.scene_depth.view, temporal.scene_depth.format,
			temporal.raytraced_color.view, temporal.motion_vectors.view, temporal.path_depth.view);
    if (r_rayTracing->integer == 2)
        vk_pt_rr_initialize(output_width, output_height);

	temporal.scene_render_pass = create_render_pass(VK_ATTACHMENT_LOAD_OP_CLEAR,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_ATTACHMENT_STORE_OP_STORE, temporal_depth_format);
	temporal.ui_render_pass = create_render_pass(VK_ATTACHMENT_LOAD_OP_LOAD,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
		VK_ATTACHMENT_STORE_OP_DONT_CARE, vk.fmt_DepthStencil);
	VkImageView attachments[] = { temporal.scene_color.view, temporal.scene_depth.view };
	VkFramebufferCreateInfo framebuffer;
	memset(&framebuffer, 0, sizeof(framebuffer));
	framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebuffer.renderPass = temporal.scene_render_pass;
	framebuffer.attachmentCount = 2;
	framebuffer.pAttachments = attachments;
	framebuffer.width = render_width;
	framebuffer.height = render_height;
	framebuffer.layers = 1;
	VK_CHECK(qvkCreateFramebuffer(vk.device, &framebuffer, NULL, &temporal.scene_framebuffer));
	ri.Printf(PRINT_ALL, "VQ3 Evolution render targets: %ux%u scene, %ux%u output\n",
		render_width, render_height, output_width, output_height);
#else
	(void)render_width; (void)render_height; (void)output_width; (void)output_height;
#endif
}

void vk_temporal_shutdown(void)
{
	// Clear external references and release NGX features while tagged images
	// still exist. RE_Shutdown drains GPU work before reaching this function;
	// initialization-failure cleanup has not submitted a frame yet.
	vk_sl_release_frame_resources();
#ifdef USE_TEMPORAL_RENDER_TARGETS
	if (temporal.scene_framebuffer) qvkDestroyFramebuffer(vk.device, temporal.scene_framebuffer, NULL);
	if (temporal.scene_render_pass) qvkDestroyRenderPass(vk.device, temporal.scene_render_pass, NULL);
	if (temporal.ui_render_pass) qvkDestroyRenderPass(vk.device, temporal.ui_render_pass, NULL);
	vk_rt_shutdown();
	vk_sharpen_shutdown();
	vk_bloom_shutdown();
	vk_nv_shutdown();
	destroy_image(&temporal.motion_vectors);
    destroy_image(&temporal.path_depth);
	destroy_image(&temporal.sharpened_color);
	destroy_image(&temporal.bloomed_color);
	destroy_image(&temporal.nv_color);
	destroy_image(&temporal.bloom_q_b);
	destroy_image(&temporal.bloom_q_a);
	destroy_image(&temporal.bloom_half);
	destroy_image(&temporal.output_color);
	destroy_image(&temporal.neural_color);
	destroy_image(&temporal.raytraced_color);
	destroy_image(&temporal.scene_depth);
	destroy_image(&temporal.scene_color);
#endif
	memset(&temporal, 0, sizeof(temporal));
}

qboolean vk_temporal_active(void) { return temporal.active; }
qboolean vk_temporal_scene_pass_active(void) { return temporal.scene_pass; }

/* A different depth attachment format requires compatible graphics pipelines. */
VkRenderPass vk_temporal_pipeline_render_pass(void)
{
	return temporal.scene_depth.format != vk.fmt_DepthStencil ?
		temporal.scene_render_pass : VK_NULL_HANDLE;
}
uint32_t vk_temporal_render_width(void) { return temporal.render_width; }
uint32_t vk_temporal_render_height(void) { return temporal.render_height; }

void vk_temporal_begin_frame(void)
{
#ifdef USE_TEMPORAL_RENDER_TARGETS
	if (!temporal.active) return;
	temporal.frame_index++;
	temporal.scene_drawn = qfalse;
	temporal.have_view = qfalse;
	temporal.ui_pass = qfalse;
	temporal.scene_pass = qtrue;
	temporal.jitter_x = 0.0f;
	temporal.jitter_y = 0.0f;
#ifdef USE_NVIDIA_STREAMLINE
	if (r_dlss->integer || r_dlssNeuralRendering->integer ||
		r_dlssFrameGeneration->integer) {
	uint32_t phase_count = 8;
	if (temporal.render_width)
		phase_count = (uint32_t)(8.0f * (float)temporal.output_width *
			(float)temporal.output_width / ((float)temporal.render_width *
			(float)temporal.render_width));
	if (phase_count < 8) phase_count = 8;
	const uint32_t phase = ((temporal.frame_index - 1) % phase_count) + 1;
	temporal.jitter_x = halton(phase, 2) - 0.5f;
	temporal.jitter_y = halton(phase, 3) - 0.5f;
	}
#endif
	vk_rt_begin_frame();
	if (temporal.scene_depth.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
		record_image_layout_transition(vk.command_buffer,
			temporal.scene_depth.image, temporal.scene_depth.aspect,
			VK_ACCESS_SHADER_READ_BIT, temporal.scene_depth.layout,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		temporal.scene_depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	begin_pass(temporal.scene_render_pass, temporal.scene_framebuffer,
		temporal.render_width, temporal.render_height);
#endif
}

void vk_temporal_mark_scene_drawn(void)
{
	if (temporal.scene_pass) temporal.scene_drawn = qtrue;
}

void vk_temporal_prepare_view(float *projection_matrix, const float *view_matrix,
	const float *camera_origin, const float *camera_axis, float camera_near,
	float camera_far, float fov_y_degrees, float aspect, qboolean is_portal)
{
#ifdef USE_TEMPORAL_RENDER_TARGETS
	if (!temporal.scene_pass || !projection_matrix) return;
	if (!is_portal) {
		memcpy(temporal.projection, projection_matrix, sizeof(temporal.projection));
		memcpy(temporal.view, view_matrix, sizeof(temporal.view));
		memcpy(temporal.camera_origin, camera_origin, sizeof(temporal.camera_origin));
		memcpy(temporal.camera_axis, camera_axis, sizeof(temporal.camera_axis));
		temporal.camera_near = camera_near;
		temporal.camera_far = camera_far;
		temporal.camera_fov_y = fov_y_degrees * (float)(M_PI / 180.0);
		temporal.camera_aspect = aspect;
		temporal.have_view = qtrue;
	}
	projection_matrix[8] += 2.0f * temporal.jitter_x / (float)temporal.render_width;
	projection_matrix[9] += 2.0f * temporal.jitter_y / (float)temporal.render_height;
	if (!is_portal)
		memcpy(temporal.render_projection, projection_matrix,
			sizeof(temporal.render_projection));
#else
	(void)projection_matrix; (void)view_matrix; (void)camera_origin; (void)camera_axis;
	(void)camera_near; (void)camera_far; (void)fov_y_degrees; (void)aspect; (void)is_portal;
#endif
}

void vk_temporal_begin_ui(void)
{
    temporal_image_t rr_color = {0};
    qboolean rr_evaluated = qfalse;
#ifdef USE_TEMPORAL_RENDER_TARGETS
	if (!temporal.active || !temporal.scene_pass) return;
	qvkCmdEndRenderPass(vk.command_buffer);
	temporal.scene_pass = qfalse;
	temporal.scene_color.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	temporal.scene_depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	const qboolean frame_generation_valid = temporal.scene_drawn && temporal.have_view;

	/*
	 * DLSS-G must know whether this viewport is active before receiving the
	 * frame's constants and tags.  Enabling it afterwards leaves the first
	 * gameplay present configured as an ordinary application frame.
	 */
	vk_sl_set_frame_generation_active(frame_generation_valid);
	vk_sl_tag_backbuffer_extent(vk.command_buffer,
		temporal.output_width, temporal.output_height);

	qboolean neural_evaluated = qfalse;
	qboolean evaluated = qfalse;
	qboolean sharpened = qfalse;
	qboolean raytraced = qfalse;
	if (frame_generation_valid && temporal.raytraced_color.image) {
		record_image_layout_transition(vk.command_buffer,
			temporal.scene_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, temporal.scene_color.layout,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
		temporal.scene_color.layout = VK_IMAGE_LAYOUT_GENERAL;
		record_image_layout_transition(vk.command_buffer,
			temporal.scene_depth.image, temporal.scene_depth.aspect,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			temporal.scene_depth.layout, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.scene_depth.layout = VK_IMAGE_LAYOUT_GENERAL;
		raytraced = vk_rt_record(vk.command_buffer, temporal.render_projection,
			temporal.camera_origin, temporal.camera_axis, tr.sunDirection,
			temporal.camera_near, temporal.camera_far, temporal.reset);
		if (raytraced) {
			record_image_layout_transition(vk.command_buffer,
				temporal.raytraced_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
				VK_ACCESS_SHADER_WRITE_BIT, temporal.raytraced_color.layout,
				VK_ACCESS_SHADER_READ_BIT, temporal.raytraced_color.layout);
		}
		record_image_layout_transition(vk.command_buffer,
			temporal.scene_depth.image, temporal.scene_depth.aspect,
			VK_ACCESS_SHADER_READ_BIT, temporal.scene_depth.layout,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
		temporal.scene_depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	if (frame_generation_valid) {
		temporal_image_t *pre_dlss = raytraced ? &temporal.raytraced_color :
			&temporal.scene_color;
		vk_sl_frame_resources_t resources;
		memset(&resources, 0, sizeof(resources));
		resources.command_buffer = vk.command_buffer;
		resources.color_input = pre_dlss->image;
		resources.color_input_view = pre_dlss->view;
		resources.color_output = temporal.output_color.image;
		resources.color_output_view = temporal.output_color.view;
		resources.depth = temporal.scene_depth.image;
		resources.depth_view = temporal.scene_depth.view;
		resources.motion_vectors = temporal.motion_vectors.image;
		resources.motion_vectors_view = temporal.motion_vectors.view;
		resources.color_format = pre_dlss->format;
		resources.depth_format = temporal.scene_depth.format;
		resources.motion_vectors_format = temporal.motion_vectors.format;
		resources.color_input_layout = pre_dlss->layout;
		resources.color_output_layout = temporal.output_color.layout;
		resources.depth_layout = temporal.scene_depth.layout;
		resources.motion_vectors_layout = temporal.motion_vectors.layout;
		resources.render_width = temporal.render_width;
		resources.render_height = temporal.render_height;
		resources.output_width = temporal.output_width;
		resources.output_height = temporal.output_height;
		resources.projection_matrix = temporal.projection;
		resources.view_matrix = temporal.view;
		resources.camera_origin = temporal.camera_origin;
		resources.camera_axis = temporal.camera_axis;
		resources.camera_near = temporal.camera_near;
		resources.camera_far = temporal.camera_far;
		resources.camera_fov_y = temporal.camera_fov_y;
		resources.camera_aspect = temporal.camera_aspect;
		resources.jitter_x = temporal.jitter_x;
		resources.jitter_y = temporal.jitter_y;
		resources.reset = temporal.reset;

		if (temporal.neural_color.image && vk_sl_neural_rendering_supported()) {
			if (!raytraced) {
				record_image_layout_transition(vk.command_buffer,
					temporal.scene_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
					temporal.scene_color.layout, VK_ACCESS_SHADER_READ_BIT,
					VK_IMAGE_LAYOUT_GENERAL);
				temporal.scene_color.layout = VK_IMAGE_LAYOUT_GENERAL;
			}
			record_image_layout_transition(vk.command_buffer,
				temporal.scene_depth.image,
				temporal.scene_depth.aspect,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				temporal.scene_depth.layout, VK_ACCESS_SHADER_READ_BIT,
				VK_IMAGE_LAYOUT_GENERAL);
			temporal.scene_depth.layout = VK_IMAGE_LAYOUT_GENERAL;
			resources.color_input = pre_dlss->image;
			resources.color_input_view = pre_dlss->view;
			resources.color_format = pre_dlss->format;
			resources.color_input_layout = pre_dlss->layout;
			resources.depth_layout = temporal.scene_depth.layout;
			resources.color_output = temporal.neural_color.image;
			resources.color_output_view = temporal.neural_color.view;
			resources.color_output_layout = temporal.neural_color.layout;
			resources.output_width = temporal.render_width;
			resources.output_height = temporal.render_height;
			neural_evaluated = vk_sl_evaluate_neural_rendering(&resources);
			if (neural_evaluated) {
				record_image_layout_transition(vk.command_buffer,
					temporal.neural_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
					VK_ACCESS_SHADER_WRITE_BIT, temporal.neural_color.layout,
					VK_ACCESS_SHADER_READ_BIT, temporal.neural_color.layout);
			}
			record_image_layout_transition(vk.command_buffer,
				temporal.scene_depth.image,
				temporal.scene_depth.aspect,
				VK_ACCESS_SHADER_READ_BIT, temporal.scene_depth.layout,
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
			temporal.scene_depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			resources.depth_layout = temporal.scene_depth.layout;
		}

		resources.color_input = neural_evaluated ? temporal.neural_color.image :
			pre_dlss->image;
		resources.color_input_view = neural_evaluated ? temporal.neural_color.view :
			pre_dlss->view;
		resources.color_format = neural_evaluated ? temporal.neural_color.format :
			pre_dlss->format;
		resources.color_input_layout = neural_evaluated ? temporal.neural_color.layout :
			pre_dlss->layout;
		resources.color_output = temporal.output_color.image;
		resources.color_output_view = temporal.output_color.view;
		resources.color_output_layout = temporal.output_color.layout;
		resources.output_width = temporal.output_width;
		resources.output_height = temporal.output_height;
		/* Streamline's camera-motion pass samples depth before NGX evaluates
		 * DLSS. Keep the tagged depth readable through present (also for FG). */
		record_image_layout_transition(vk.command_buffer,
			temporal.scene_depth.image, temporal.scene_depth.aspect,
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			temporal.scene_depth.layout, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		temporal.scene_depth.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		resources.depth_layout = temporal.scene_depth.layout;
		/* NGX may clear its output with a transfer command before dispatching.
		 * Supply a writable GENERAL image and include both kinds of writes. */
		record_image_layout_transition(vk.command_buffer,
			temporal.output_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			temporal.output_color.layout,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.output_color.layout = VK_IMAGE_LAYOUT_GENERAL;
		resources.color_output_layout = temporal.output_color.layout;
        if (raytraced && r_rayTracing->integer == 2) {
            record_image_layout_transition(vk.command_buffer, temporal.path_depth.image,
                VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
            record_image_layout_transition(vk.command_buffer, temporal.motion_vectors.image,
                VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
            resources.depth = temporal.path_depth.image;
            resources.depth_view = temporal.path_depth.view;
            resources.depth_format = temporal.path_depth.format;
            resources.depth_layout = VK_IMAGE_LAYOUT_GENERAL;
            resources.camera_motion_included = qtrue;
            resources.reset = resources.reset || vk_pt_history_reset();
        }
        if (raytraced && r_rayTracing->integer == 2) {
            rr_evaluated = vk_pt_rr_evaluate(&resources, &rr_color.image, &rr_color.view);
            if (rr_evaluated) {
                rr_color.format = VK_FORMAT_R8G8B8A8_UNORM;
                rr_color.layout = VK_IMAGE_LAYOUT_GENERAL;
                rr_color.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            }
        }
		evaluated = rr_evaluated || vk_sl_evaluate_dlss(&resources);
	}

	if (evaluated && !rr_evaluated && temporal.sharpen_ready && r_dlssSharpness->value > 0.0f) {
		record_image_layout_transition(vk.command_buffer,
			temporal.output_color.image, VK_IMAGE_ASPECT_COLOR_BIT,
			VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
			temporal.output_color.layout, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL);
		temporal.output_color.layout = VK_IMAGE_LAYOUT_GENERAL;
		sharpened = vk_sharpen_record(vk.command_buffer,
			r_dlssSharpness->value, temporal.output_width,
			temporal.output_height);
	}

	temporal_image_t *source = rr_evaluated ? &rr_color : (sharpened ? &temporal.sharpened_color :
		(evaluated ? &temporal.output_color :
		(neural_evaluated ? &temporal.neural_color :
		(raytraced ? &temporal.raytraced_color : &temporal.scene_color))));
	const VkAccessFlags source_access = sharpened ? VK_ACCESS_SHADER_WRITE_BIT :
		(evaluated || neural_evaluated || raytraced ?
		(VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT) :
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

	/* Post-processing bloom on the final tone-mapped frame. All upscale paths
	 * converge here, so the composite runs once regardless of how the frame
	 * was produced and still lets the blit (and DLSS-G tagging) use the same
	 * source image untouched. */
	qboolean bloomed = qfalse;
	temporal_image_t *blit_source = source;
	VkAccessFlags blit_source_access = source_access;
	const qboolean source_at_output = (evaluated || rr_evaluated ||
		neural_evaluated ||
		(temporal.output_width == temporal.render_width &&
		 temporal.output_height == temporal.render_height));
	if (temporal.bloom_ready && r_postBloom->integer &&
		r_postBloomStrength->value > 0.0f && source_at_output) {
		VkImageLayout prev_source_layout = source->layout;
		record_image_layout_transition(vk.command_buffer, source->image,
			VK_IMAGE_ASPECT_COLOR_BIT, source_access, source->layout,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
		source->layout = VK_IMAGE_LAYOUT_GENERAL;
		temporal.bloomed_color.layout = VK_IMAGE_LAYOUT_GENERAL;
		bloomed = vk_bloom_record(vk.command_buffer, source->view,
			temporal.output_width, temporal.output_height,
			r_postBloomStrength->value, r_postBloomThreshold->value,
			r_postBloomDebug->integer);
		record_image_layout_transition(vk.command_buffer, source->image,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, source_access, prev_source_layout);
		source->layout = prev_source_layout;
		if (bloomed) {
			blit_source = &temporal.bloomed_color;
			blit_source_access =
				VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
	}

	/* Night-vision post-pass. Reads the converged frame (bloomed when bloom
	 * ran, otherwise the source) and becomes the blit source while active.
	 * The legacy overlay shaders are auto-detected in the backend and set
	 * the R_NvOverlayActive() flag; r_nvOverride forces the effect on. */
	if (temporal.nv_ready && source_at_output &&
		(r_nvOverride->integer ||
		 (r_nvNightVision->integer && R_NvOverlayActive()))) {
		temporal_image_t *nv_input = bloomed ? &temporal.bloomed_color : source;
		const VkAccessFlags nv_input_access = bloomed ?
			(VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT) :
			source_access;
		VkImageLayout prev_nv_layout = nv_input->layout;
		record_image_layout_transition(vk.command_buffer, nv_input->image,
			VK_IMAGE_ASPECT_COLOR_BIT, nv_input_access, nv_input->layout,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL);
		nv_input->layout = VK_IMAGE_LAYOUT_GENERAL;
		temporal.nv_color.layout = VK_IMAGE_LAYOUT_GENERAL;
		qboolean nv_applied = vk_nv_record(vk.command_buffer, nv_input->view,
			temporal.output_width, temporal.output_height,
			r_nvBrightness->value, r_nvGrain->value, tr.refdef.floatTime,
			(uint32_t)r_nvTint->integer, (uint32_t)r_nvDebug->integer,
			r_nvVignette->value);
		record_image_layout_transition(vk.command_buffer, nv_input->image,
			VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT,
			VK_IMAGE_LAYOUT_GENERAL, nv_input_access, prev_nv_layout);
		nv_input->layout = prev_nv_layout;
		if (nv_applied) {
			blit_source = &temporal.nv_color;
			blit_source_access =
				VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
		}
	}

	record_image_layout_transition(vk.command_buffer, blit_source->image, VK_IMAGE_ASPECT_COLOR_BIT,
		blit_source_access,
		blit_source->layout, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	record_image_layout_transition(vk.command_buffer,
		vk.swapchain_images_array[vk.idx_swapchain_image], VK_IMAGE_ASPECT_COLOR_BIT,
		0, VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_TRANSFER_WRITE_BIT,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	VkImageBlit blit;
	memset(&blit, 0, sizeof(blit));
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.srcOffsets[1].x = evaluated ? (int32_t)temporal.output_width : (int32_t)temporal.render_width;
	blit.srcOffsets[1].y = evaluated ? (int32_t)temporal.output_height : (int32_t)temporal.render_height;
	blit.srcOffsets[1].z = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;
	blit.dstOffsets[1].x = (int32_t)temporal.output_width;
	blit.dstOffsets[1].y = (int32_t)temporal.output_height;
	blit.dstOffsets[1].z = 1;
	qvkCmdBlitImage(vk.command_buffer, blit_source->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		vk.swapchain_images_array[vk.idx_swapchain_image], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit, VK_FILTER_LINEAR);
	record_image_layout_transition(vk.command_buffer, blit_source->image, VK_IMAGE_ASPECT_COLOR_BIT,
		VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, blit_source->layout);
	record_image_layout_transition(vk.command_buffer,
		vk.swapchain_images_array[vk.idx_swapchain_image], VK_IMAGE_ASPECT_COLOR_BIT,
		VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	if (frame_generation_valid)
		vk_sl_tag_hudless(vk.command_buffer, source->image, source->view, source->format,
			source->layout, evaluated ? temporal.output_width : temporal.render_width,
			evaluated ? temporal.output_height : temporal.render_height, source->usage);
	begin_pass(temporal.ui_render_pass, vk.framebuffers[vk.idx_swapchain_image],
		temporal.output_width, temporal.output_height);
	temporal.ui_pass = qtrue;
	temporal.reset = temporal.scene_drawn ? qfalse : qtrue;
#endif
}

void vk_temporal_end_frame(void)
{
#ifdef USE_TEMPORAL_RENDER_TARGETS
	if (!temporal.active) return;
	if (temporal.scene_pass) vk_temporal_begin_ui();
	if (temporal.ui_pass) {
		qvkCmdEndRenderPass(vk.command_buffer);
		temporal.ui_pass = qfalse;
	}
	/* The NV overlay flag is backend state set per drawsurf; it belongs to a
	 * single frame, so clear it once the frame's commands are exhausted. */
	R_NvOverlayClear();
#endif
}
