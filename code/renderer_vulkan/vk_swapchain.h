#ifndef VK_SWAPCHAIN_H_
#define VK_SWAPCHAIN_H_

#include "VKimpl.h"

void vk_request_swapchain_restart(const char *reason);
qboolean vk_swapchain_restart_pending(void);
void vk_createSwapChain(VkDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR surface_format);


#endif
