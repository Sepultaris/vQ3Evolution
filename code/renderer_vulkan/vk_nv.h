#ifndef VK_NV_H_
#define VK_NV_H_

#include "../qcommon/q_shared.h"
#include "VKimpl.h"

qboolean vk_nv_initialize(VkImageView output_view, uint32_t width,
	uint32_t height);
void vk_nv_shutdown(void);
qboolean vk_nv_ready(void);
qboolean vk_nv_record(VkCommandBuffer command_buffer, VkImageView src_view,
	uint32_t width, uint32_t height, float gain, float grain, float time,
	uint32_t tint, uint32_t debug, float vignette);

#endif