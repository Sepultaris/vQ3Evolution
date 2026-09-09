#ifndef VK_DLSSNR_H_
#define VK_DLSSNR_H_

#include "VKimpl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_sl_frame_resources_s;

qboolean vk_dlssnr_attach(VkInstance instance, VkPhysicalDevice physical_device,
	VkDevice device, PFN_vkGetInstanceProcAddr get_instance_proc_addr,
	PFN_vkGetDeviceProcAddr get_device_proc_addr);
/* Drain GPU work first. Closes the snippet and its parameters while the
 * Streamline-owned NGX core/device are still alive; safe to call twice. */
void vk_dlssnr_shutdown(void);
void vk_dlssnr_unload(void);
qboolean vk_dlssnr_supported(void);
void vk_dlssnr_configure(int mode, uint32_t width, uint32_t height);
qboolean vk_dlssnr_prepare(VkCommandBuffer command_buffer);
qboolean vk_dlssnr_evaluate(const struct vk_sl_frame_resources_s *resources);
qboolean vk_sl_verify_nvidia_signature(const wchar_t *path);

#ifdef __cplusplus
}
#endif

#endif
