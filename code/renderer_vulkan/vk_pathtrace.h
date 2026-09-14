#ifndef VK_PATHTRACE_H_
#define VK_PATHTRACE_H_

#include "tr_local.h"
#include "vk_streamline.h"

#define PT_MAX_TEXTURES 512
#define PT_TEST_VERTICES 128
#define PT_TEST_INDICES 192
const char *vk_pt_builtin_shader(const char *name);
void vk_pt_test_scene(float *positions, uint32_t *indices, uint32_t *vertex_cursor, uint32_t *index_cursor);
void vk_pt_test_motion(float *positions, uint32_t *indices, uint32_t *vertex_cursor, uint32_t *index_cursor);
void vk_pt_load_media(const byte *file, int length, const dheader_t *header);
qboolean vk_pt_initialize(uint32_t width, uint32_t height,
    uint32_t max_vertices, uint32_t max_indices,
    VkImageView color, VkImageView depth, VkImageView output,
    VkImageView motion, VkImageView path_depth);
qboolean vk_pt_history_reset(void);
qboolean vk_pt_rr_initialize(uint32_t output_width, uint32_t output_height);
qboolean vk_pt_rr_evaluate(const vk_sl_frame_resources_t *resources, VkImage *output, VkImageView *view, qboolean clean_output);
void vk_pt_shutdown(void);
void vk_pt_info_f(void);
void vk_pt_profile(VkCommandBuffer cmd, uint32_t point);
double vk_pt_software_clock(void);
void vk_pt_software_scene_time(double start);
uint32_t vk_pt_partition_world(uint32_t *indices, uint32_t count);
void vk_pt_partition_dynamic(uint32_t *indices, uint32_t count, uint32_t starts[5]);
void vk_pt_begin_world(uint32_t vertices, uint32_t indices);
void vk_pt_world_vertex(uint32_t vertex, const float *normal, const float *uv, byte alpha);
void vk_pt_world_surface(uint32_t first_index, uint32_t index_count, const msurface_t *surface);
void vk_pt_begin_frame(void);
qboolean vk_pt_portal_shader(const shader_t *shader);
void vk_pt_software_scene(VkBuffer nodes, VkDeviceSize node_size,
    VkBuffer links, VkDeviceSize link_size, VkBuffer triangles, VkDeviceSize triangle_size);
void vk_pt_capture(uint32_t first_vertex, uint32_t first_index,
    uint32_t vertex_count, uint32_t index_count,
    const float (*normals)[4], const float (*uv)[2][2],
    const float (*axis)[3], shader_t *shader,
    const float *world_positions, const uint32_t *local_indices);
qboolean vk_pt_record(VkCommandBuffer cmd, VkAccelerationStructureKHR scene,
    VkBuffer positions, VkBuffer indices, const float *cpu_positions,
    const uint32_t *cpu_indices, uint32_t vertex_count, uint32_t index_count,
    const float *projection, const float *origin, const float *axis,
    const float *sun, float near_distance, float far_distance, float ray_distance,
    qboolean reset_history);

#endif
