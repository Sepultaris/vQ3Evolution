#ifndef VK_BLOOM_H_
#define VK_BLOOM_H_

#include "../qcommon/q_shared.h"
#include "VKimpl.h"

qboolean vk_bloom_initialize(VkImageView half_view, VkImageView qa_view,
	VkImageView qb_view, VkImageView output_view, uint32_t width,
	uint32_t height);
void vk_bloom_shutdown(void);
qboolean vk_bloom_ready(void);
qboolean vk_bloom_record(VkCommandBuffer command_buffer, VkImageView src_view,
	uint32_t width, uint32_t height, float strength, float threshold,
	uint32_t debug);

#endif