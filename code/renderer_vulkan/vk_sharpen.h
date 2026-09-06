#ifndef VK_SHARPEN_H_
#define VK_SHARPEN_H_

#include "../qcommon/q_shared.h"
#include "VKimpl.h"

qboolean vk_sharpen_initialize(VkImageView input_view, VkImageView output_view);
void vk_sharpen_shutdown(void);
qboolean vk_sharpen_record(VkCommandBuffer command_buffer, float sharpness,
	uint32_t width, uint32_t height);

#endif
