#ifndef VK_RAYTRACING_H_
#define VK_RAYTRACING_H_

#include "tr_local.h"

qboolean vk_rt_configure_device(VkPhysicalDevice physical_device,
	const VkExtensionProperties *extensions, uint32_t extension_count,
	PFN_vkGetPhysicalDeviceFeatures2 get_features2, void **device_features);
uint32_t vk_rt_device_extension_count(void);
const char *vk_rt_device_extension(uint32_t index);
qboolean vk_rt_supported(void);

qboolean vk_rt_initialize(uint32_t width, uint32_t height,
	VkImageView color_view, VkFormat color_format,
	VkImageView depth_view, VkFormat depth_format,
	VkImageView output_view, VkImageView motion_view, VkImageView path_depth_view);
void vk_rt_load_world(void);
void vk_rt_shutdown(void);
void vk_rt_begin_frame(void);
void vk_rt_capture_geometry(const float (*vertices)[4], uint32_t vertex_count,
	const uint32_t *indices, uint32_t index_count, const float *origin,
	const float (*axis)[3], qboolean opaque, const float (*normals)[4],
	const float (*uv)[2][2], shader_t *shader);
qboolean vk_rt_record(VkCommandBuffer command_buffer,
	const float *projection, const float *camera_origin,
	const float *camera_axis, const float *sun_direction,
	float near_distance, float far_distance, qboolean reset_history);

#endif
