#ifndef VK_STREAMLINE_H_
#define VK_STREAMLINE_H_

#include "VKimpl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin C boundary around NVIDIA Streamline.  Keeping the SDK's C++ types out
 * of the renderer lets the rest of the Vulkan backend remain C.
 */
qboolean vk_sl_initialize(void);
void vk_sl_shutdown(void);
void vk_sl_unload(void);
qboolean vk_sl_is_initialized(void);

uint32_t vk_sl_instance_extension_count(void);
const char *vk_sl_instance_extension(uint32_t index);
uint32_t vk_sl_device_extension_count(void);
const char *vk_sl_device_extension(uint32_t index);

void vk_sl_check_feature_support(VkInstance instance, VkPhysicalDevice physical_device,
	VkDevice device);
void *vk_sl_get_instance_proc_addr(VkInstance instance, const char *name);
void *vk_sl_get_device_proc_addr(VkDevice device, const char *name);

qboolean vk_sl_dlss_supported(void);
qboolean vk_sl_neural_rendering_supported(void);
qboolean vk_sl_frame_generation_supported(void);
qboolean vk_sl_reflex_supported(void);
const char *vk_sl_status(void);
qboolean vk_sl_configure(int dlss_mode, int frame_generation, int reflex_mode,
	uint32_t output_width, uint32_t output_height,
	uint32_t *render_width, uint32_t *render_height);

typedef struct vk_sl_frame_resources_s {
	VkCommandBuffer command_buffer;
	VkImage color_input;
	VkImageView color_input_view;
	VkImage color_output;
	VkImageView color_output_view;
	VkImage depth;
	VkImageView depth_view;
	VkImage motion_vectors;
	VkImageView motion_vectors_view;
	VkFormat color_format;
	VkFormat depth_format;
	VkFormat motion_vectors_format;
	VkImageLayout color_input_layout;
	VkImageLayout color_output_layout;
	VkImageLayout depth_layout;
	VkImageLayout motion_vectors_layout;
	uint32_t render_width;
	uint32_t render_height;
	uint32_t output_width;
	uint32_t output_height;
	const float *projection_matrix;
	const float *view_matrix;
	const float *camera_origin;
	const float *camera_axis;
	float camera_near;
	float camera_far;
	float camera_fov_y;
	float camera_aspect;
	float jitter_x;
	float jitter_y;
	qboolean reset;
    qboolean camera_motion_included;
} vk_sl_frame_resources_t;

qboolean vk_sl_begin_frame(uint32_t frame_index);
qboolean vk_sl_evaluate_dlss(const vk_sl_frame_resources_t *resources);
void vk_sl_configure_neural_rendering(int mode, uint32_t width, uint32_t height);
qboolean vk_sl_prepare_neural_rendering(VkCommandBuffer command_buffer);
qboolean vk_sl_evaluate_neural_rendering(const vk_sl_frame_resources_t *resources);
void vk_sl_tag_hudless(VkCommandBuffer command_buffer, VkImage image,
	VkImageView view, VkFormat format, VkImageLayout layout,
	uint32_t width, uint32_t height, VkImageUsageFlags usage);
void vk_sl_tag_backbuffer_extent(VkCommandBuffer command_buffer,
	uint32_t width, uint32_t height);
void vk_sl_set_frame_generation_active(qboolean active);
void vk_sl_mark_render_submit(qboolean start);
void vk_sl_mark_present(qboolean start);

#ifdef __cplusplus
}
#endif

#endif
