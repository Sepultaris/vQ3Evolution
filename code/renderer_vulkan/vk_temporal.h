#ifndef VK_TEMPORAL_H_
#define VK_TEMPORAL_H_

#include "VKimpl.h"

void vk_temporal_initialize(uint32_t render_width, uint32_t render_height,
	uint32_t output_width, uint32_t output_height);
void vk_temporal_shutdown(void);
qboolean vk_temporal_active(void);
qboolean vk_temporal_scene_pass_active(void);
VkRenderPass vk_temporal_pipeline_render_pass(void);
uint32_t vk_temporal_render_width(void);
uint32_t vk_temporal_render_height(void);

void vk_temporal_begin_frame(void);
void vk_temporal_mark_scene_drawn(void);
void vk_temporal_begin_ui(void);
void vk_temporal_end_frame(void);

void vk_temporal_prepare_view(float *projection_matrix, const float *view_matrix,
	const float *camera_origin, const float *camera_axis, float camera_near,
	float camera_far, float fov_y_degrees, float aspect, qboolean is_portal);

#endif
