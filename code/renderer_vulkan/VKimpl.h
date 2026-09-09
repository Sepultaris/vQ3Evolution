#ifndef VKIMPL_H_
#define VKIMPL_H_


/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/


#define VK_NO_PROTOTYPES
#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan/vulkan.h"

void vk_createWindow(void);
void vk_getDrawableSize(int *width, int *height);
void vk_destroyWindow(void);

void vk_getInstanceProcAddrImpl(void);

void vk_createSurfaceImpl(void);

void vk_minimizeWindow( void );

#endif
