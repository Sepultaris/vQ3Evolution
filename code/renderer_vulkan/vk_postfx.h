#ifndef VK_POSTFX_H
#define VK_POSTFX_H
#include "../qcommon/q_shared.h"
#include "../renderercommon/postfx.h"
#include "VKimpl.h"
typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    VkImageUsageFlags usage;
    VkImageAspectFlags aspect;
    VkImageLayout layout;
} postfxImage_t;
typedef struct {
    postfxImage_t *image;
    VkAccessFlags access;
    float projectionJitter[4]; // projection[10], [14], jitter sampling offset in UV.
    postfxImage_t *motion;
    VkAccessFlags motionAccess;
    float motionInfo[4]; // traced vectors present, history valid, frame seconds, reconstructed color.
    float motionJitter[2]; // negative camera jitter in normalized input units.
    float previousClipRows[3][4]; // x, y, w rows mapping current unjittered clip to previous clip.
} postfxDepth_t;
void vk_postfx_initialize(uint32_t width,uint32_t height);
void vk_postfx_shutdown(void);
qboolean vk_postfx_enabled(void);
qboolean vk_postfx_get_effect(int index,postfxEffect_t *effect);
postfxImage_t *vk_postfx_record(VkCommandBuffer cmd,postfxImage_t *input,VkAccessFlags access,float time,uint32_t frame,const postfxDepth_t *depth);
void vk_postfx_info_f(void);
void vk_postfx_reload_f(void);
#endif
