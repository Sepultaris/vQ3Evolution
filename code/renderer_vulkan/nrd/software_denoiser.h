/* VQ3 Evolution's optional compute-denoiser ABI. No NVIDIA SDK types or code.
 * The adapter is built separately; the engine builds and runs without it. */
#ifndef VQE_SOFTWARE_DENOISER_H
#define VQE_SOFTWARE_DENOISER_H
#include <stdint.h>
#ifndef VK_VERSION_1_0
#include <vulkan/vulkan.h>
#endif
#define VQE_DENOISER_ABI 1u
enum {
    VQE_DENOISE_DIFFUSE, VQE_DENOISE_SPECULAR, VQE_DENOISE_POSITION,
    VQE_DENOISE_ALBEDO, VQE_DENOISE_NORMAL, VQE_DENOISE_MOTION,
    VQE_DENOISE_TRANSMISSION, VQE_DENOISE_EMISSION, VQE_DENOISE_METADATA,
    VQE_DENOISE_LIGHT_CHANGE, VQE_DENOISE_NATIVE_DIFFUSE,
    VQE_DENOISE_NATIVE_SPECULAR, VQE_DENOISE_BUFFERS
};
typedef struct {
    uint32_t abi, width, height;
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    PFN_vkGetInstanceProcAddr get_instance_proc;
    PFN_vkGetDeviceProcAddr get_device_proc;
} vqe_denoiser_init_t;
typedef struct {
    VkCommandBuffer command;
    VkImageView output;
    VkBuffer buffers[VQE_DENOISE_BUFFERS];
    float camera[32]; // Same 128-byte constants as the software integrator.
    float previous[32]; // Previous camera + reconstruction controls.
    uint32_t frame, history, reset;
    float frame_ms;
} vqe_denoiser_frame_t;
typedef void *(*vqe_denoiser_create_t)(const vqe_denoiser_init_t *, char *, uint32_t);
typedef int (*vqe_denoiser_record_t)(void *, const vqe_denoiser_frame_t *, char *, uint32_t);
typedef void (*vqe_denoiser_destroy_t)(void *);
#endif
