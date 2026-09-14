#include "vk_pathtrace.h"
#include "vk_raytracing.h"
#include "vk_instance.h"
#include "vk_streamline.h"
#include "vk_shaders.h"
#include "vk_image.h"
#include "vk_image_sampler.h"
#include "tr_globals.h"
#include "tr_backend.h"
#include "tr_shader.h"
#include "tr_light.h"
#include "tr_cvar.h"
#include "R_Parser.h"
#include "pt_blue_noise.h"
#include "../renderercommon/muzzle_flash.h"
#include "pt_push_debug.h"
#include "pt_weapon_flags.h"
#include "pt_portal_transform.h"

#define PT_MAX_EMITTERS 8192
#define PT_MAX_LIGHTS 32
#define PT_MAX_MAP_LIGHTS 1024
#define PT_POINT_CAPACITY (PT_MAX_LIGHTS + PT_MAX_MAP_LIGHTS)
#define PT_MAX_MOTION_SURFACES 4096
#define PT_MOTION_BUCKETS 2048
#define PT_LIGHT_CELLS 512

/* std430 layouts shared with pathtrace.comp. World RGB lighting is excluded;
 * authored vertex alpha is material data (including terrain blend weights). */
typedef struct { float normal[4], uv[4], previous[4], meta[4], color[4], local[4]; } pt_vertex_t;
typedef struct {
    uint32_t key[7], topology, first, count, ordinal;
    int next;
    qboolean rigidBrush;
    vec3_t origin, axis[3];
} pt_motion_surface_t;
typedef struct { float a[4], b[4], wave[4]; } pt_texmod_t;
typedef struct {
    float images[2][4], params[4], color[4], generators[4];
    float rgb_wave[4], alpha_wave[4], meta[4], vectors[2][4];
    pt_texmod_t mods[TR_MAX_TEXMODS];
} pt_layer_t;
typedef struct {
    /* composition: ordered layer count, base index, additive mask, overlay count. */
    float surface[4], emission[4], composition[4], params[4];
    float maps[4], optical[4], absorption[4];
    pt_layer_t layers[1 + MAX_SHADER_STAGES];
    /* Six outerbox texture IDs (-1 absent), cloud height, authored layer count. */
    float skybox[2][4];
} pt_material_t;
typedef struct { uint32_t primitive; float cumulative_power; } pt_emitter_t;
#include "pt_emitter_search.h"
#include "pt_alias_pdf.h"
#include "pt_emitter_geometry.h"
typedef struct {
    float origin_cell[4];
    uint32_t dimensions[4];
    float entries[PT_LIGHT_CELLS * PT_MAX_MAP_LIGHTS][4];
} pt_light_grid_t;
typedef struct { uint32_t first, count; float ior; vec3_t absorption; } pt_medium_t;
/* Separate immutable, map-sized buffer; no fog planes in per-ray local arrays. */
typedef struct { float mins[4], maxs[4], albedo[4], emission[4]; uint32_t planes[4]; } pt_fog_t;
typedef struct { uint32_t control[4]; float mins[4], maxs[4]; pt_fog_t volumes[MAX_MAP_FOGS]; } pt_fog_header_t;
typedef char pt_fog_layout_check[(sizeof(pt_fog_t) == 80 && sizeof(pt_fog_header_t) == 20528) ? 1 : -1];
/* std430 vec4 blocks, shared with both reconstruction shaders. */
typedef struct {
    float origin[4], forward[4], right[4], up[4];
    float jitter_history_reset[4];
    float options[4];
    float depth_projection[4];
    float camera_medium[4];
} pt_temporal_params_t;
typedef char pt_temporal_layout_check[(sizeof(pt_temporal_params_t) == 128) ? 1 : -1];
typedef struct {
    uint32_t counts[4];
    float selection[4];
    float positions[PT_POINT_CAPACITY][4];
    float colors[PT_POINT_CAPACITY][4];
    float cones[PT_POINT_CAPACITY][4];
    pt_emitter_t emitters[PT_MAX_EMITTERS];
    float previous_positions[PT_MAX_LIGHTS][4], previous_colors[PT_MAX_LIGHTS][4];
    float decal_bounds[2][4]; // Aggregate rejection bounds; mins.w is triangle count.
    float sky_environment[4]; // x: sky ID + 1; y: ambient; z: sun half-angle radians; w: point source radius.
    uint32_t emitter_search_control[4]; // x: conservative search enabled; y: bucket count.
    uint32_t emitter_search_ranges[PT_EMITTER_SEARCH_BUCKETS][2]; // std430 uvec4[32].
    pt_emitter_geometry_t emitter_geometry[PT_MAX_EMITTERS];
    float rocket_emission[4]; // x/y: rocket glow/light; z/w: explosion/lightning light. Same layout.
    pt_portal_t portals[PT_MAX_PORTALS];
} pt_lights_t;
typedef char pt_vertex_layout_check[(sizeof(pt_vertex_t) == 96) ? 1 : -1];
typedef char pt_layer_layout_check[(sizeof(pt_layer_t) == 352) ? 1 : -1];
typedef char pt_material_layout_check[(sizeof(pt_material_t) == 3312) ? 1 : -1];
typedef char pt_emitter_geometry_layout_check[(sizeof(pt_emitter_geometry_t) == 48 && offsetof(pt_lights_t, emitter_geometry) == 117856) ? 1 : -1];
typedef char pt_rocket_emission_layout_check[(offsetof(pt_lights_t, rocket_emission) == 117856 + PT_MAX_EMITTERS * 48) ? 1 : -1];
typedef char pt_lights_layout_check[(sizeof(pt_portal_t) == 64 && sizeof(pt_lights_t) == 96 + PT_POINT_CAPACITY * 48 + 8192 * 8 + PT_MAX_LIGHTS * 32 + 16 + 64 * 8 + PT_MAX_EMITTERS * 48 + PT_MAX_PORTALS * 64) ? 1 : -1];
typedef struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkBuffer upload;
    VkDeviceMemory upload_memory;
    void *upload_mapped;
    VkDeviceSize size;
    void *mapped;
} pt_buffer_t;
static struct {
    qboolean active, textures_dirty, logged;
    qboolean software; // Mode 1 only; mode 2 keeps its hardware pipelines.
    qboolean frozen;
    uint32_t width, height, max_vertices, max_indices;
    uint32_t world_vertices, world_indices, texture_count;
    uint32_t world_opaque_indices;
    uint32_t *partition_indices, *partition_materials;
    uint32_t frame, history, previous_hash;
    uint32_t camera_resets, scene_resets;
    uint32_t temporal_resets, previous_lights_hash, previous_radiance_hash, reconstruction_index;
    uint32_t temporal_reset_reasons[5];
    qboolean temporal_valid;
    int previous_time;
    float previous_temporal_camera[32];
    uint32_t map_light_count;
    float map_positions[PT_MAX_MAP_LIGHTS][4];
    float map_colors[PT_MAX_MAP_LIGHTS][4];
    float map_cones[PT_MAX_MAP_LIGHTS][4];
    float previous_camera[32];
    float exposure;
    vec3_t sun_color, sun_direction;
    int sky_material;
    pt_vertex_t *world_attributes;
    uint32_t *world_materials;
    qboolean world_upload_pending;
    VkDeviceSize geometry_upload_bytes;
    shader_t *material_shaders[MAX_SHADERS];
    image_t *textures[PT_MAX_TEXTURES];
    pt_buffer_t attributes, triangle_materials, materials, lights, accumulation;
    pt_buffer_t guides[3], filter[2];
    pt_buffer_t previous_guides[2], history_color[2], temporal_params;
    pt_buffer_t motion_guides[2];
    pt_buffer_t specular, visible_emission, spec_history[2], spec_filter[2];
    pt_buffer_t shading_guide, previous_shading_guide;
    pt_buffer_t light_grid, blue_noise;
    pt_buffer_t fog;
    qboolean fog_dirty;
    pt_buffer_t reflection_guide, previous_reflection_guide, reflection_motion;
    pt_buffer_t moments[2], light_change;
    uint32_t local_light_updates;
    int previous_sampling;
    float test_previous_y;
    qboolean test_motion_valid;
    qboolean sampling_dirty;
    uint32_t light_cells;
    vec3_t *motion_positions[2];
    pt_motion_surface_t motion_surfaces[2][PT_MAX_MOTION_SURFACES];
    int motion_buckets[2][PT_MOTION_BUCKETS];
    uint32_t motion_frame, motion_count[2], motion_vertices;
    uint32_t motion_matched, motion_rejected;
    float material_time, previous_material_time;
    uint32_t animated_materials, effect_materials;
    uint32_t material_count;
    uint32_t muzzle_flash_triangles;
    uint32_t rocket_triangles;
    uint32_t rocket_explosion_triangles, lightning_gun_triangles;
    qboolean materials_dirty;
    int material_program_mode;
    pt_medium_t *media;
    vec4_t *medium_planes;
    uint32_t medium_count;
    VkQueryPool profile_pool;
    PFN_vkDestroyQueryPool destroy_queries;
    PFN_vkGetQueryPoolResults get_queries;
    PFN_vkCmdResetQueryPool reset_queries;
    PFN_vkCmdWriteTimestamp write_timestamp;
    float timestamp_period;
    uint64_t timestamp_mask;
    uint32_t profile_mask;
    int profile_time;
    double software_profile_time, software_scene_cpu;
    qboolean profile_instrumented;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline lighting_pipelines[128]; // bits: BRDF reuse, early rejection, alias PDF, packed emitters, unified light loop, cached materials, dlight reservoir.
    uint32_t lighting_mode;
    qboolean lighting_failed[128], brdf_active, map_light_cull_active, alias_pdf_active, emitter_geometry_active, light_loop_active;
    VkPipeline guide_pipeline;
    VkPipeline material_cache_pipeline;
    qboolean material_cache_failed, material_cache_active;
    /* Compact transport has separate specializations for cached alias PDFs. */
    VkPipeline compact_transport_pipeline[2];
    uint32_t compact_transport_rows;
    qboolean compact_transport_failed[2], compact_transport_active;
    struct {
        pt_buffer_t state;
        VkBuffer comparison;
        VkDeviceMemory comparison_memory;
        uint32_t *mapped;
        VkDescriptorSetLayout set_layout;
        VkDescriptorPool pool;
        VkDescriptorSet set;
        VkPipelineLayout layout;
        VkPipeline pipelines[4];
        uint32_t rows;
        VkQueryPool profile_pool;
        uint32_t profile_capacity, profile_written;
        uint64_t *profile_ticks;
        unsigned char *profile_stages;
        int record_ms;
        qboolean ready, failed, active, compared;
    } staged;
    VkPipeline denoise_pipeline;
    VkPipeline temporal_pipeline;
    VkSampler sampler;
    int texture_mode_revision;
    pt_buffer_t transmission, transmission_history[2];
    pt_buffer_t transmission_guide, previous_transmission_guide, transmission_motion;
    pt_buffer_t software_metadata;
} pt;
#include "pt_software_denoise.h"
#include "pt_ray_reconstruction.h"
static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size);
#include "pt_shader_profile.h"
#include "pt_software_profile.h"
#include "pt_exposure.h"
#include "pt_pipeline_stats.h"
#include "pt_material_cache.h"
#include "pt_compact_transport.h"
#include "pt_gpu_labels.h"

static qboolean lighting_pipeline_initialize(uint32_t mode)
{
    if (mode >= ARRAY_LEN(pt.lighting_pipelines)) return qfalse;
    if (pt.lighting_pipelines[mode]) return qtrue;
    if (pt.lighting_failed[mode]) return qfalse;
    extern unsigned char pt_brdf_comp_spv[];
    extern int pt_brdf_comp_spv_size;
    extern unsigned char pathtrace_comp_spv[];
    extern int pathtrace_comp_spv_size;
    extern unsigned char pt_light_loop_comp_spv[], pt_light_loop_brdf_comp_spv[];
    extern int pt_light_loop_comp_spv_size, pt_light_loop_brdf_comp_spv_size;
    const unsigned char *code = (mode & 16) ? ((mode & 1) ? pt_light_loop_brdf_comp_spv : pt_light_loop_comp_spv) :
        ((mode & 1) ? pt_brdf_comp_spv : pathtrace_comp_spv);
    size_t code_size = (mode & 16) ? ((mode & 1) ? pt_light_loop_brdf_comp_spv_size : pt_light_loop_comp_spv_size) :
        ((mode & 1) ? pt_brdf_comp_spv_size : pathtrace_comp_spv_size);
    if (mode & 32) {
        extern unsigned char pt_cached_materials_comp_spv[], pt_cached_materials_brdf_comp_spv[];
        extern unsigned char pt_cached_materials_loop_comp_spv[], pt_cached_materials_loop_brdf_comp_spv[];
        extern int pt_cached_materials_comp_spv_size, pt_cached_materials_brdf_comp_spv_size;
        extern int pt_cached_materials_loop_comp_spv_size, pt_cached_materials_loop_brdf_comp_spv_size;
        code = (mode & 16) ? ((mode & 1) ? pt_cached_materials_loop_brdf_comp_spv : pt_cached_materials_loop_comp_spv) :
            ((mode & 1) ? pt_cached_materials_brdf_comp_spv : pt_cached_materials_comp_spv);
        code_size = (mode & 16) ? ((mode & 1) ? pt_cached_materials_loop_brdf_comp_spv_size : pt_cached_materials_loop_comp_spv_size) :
            ((mode & 1) ? pt_cached_materials_brdf_comp_spv_size : pt_cached_materials_comp_spv_size);
    }
    if (pt.software) {
        extern unsigned char pt_software_comp_spv[];
        extern int pt_software_comp_spv_size;
        code = pt_software_comp_spv;
        code_size = pt_software_comp_spv_size;
        if(sw_denoise.instance) {
            extern unsigned char pt_software_nrd_comp_spv[];
            extern int pt_software_nrd_comp_spv_size;
            code=pt_software_nrd_comp_spv; code_size=pt_software_nrd_comp_spv_size;
        }
    }
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size, .pCode = (const uint32_t *)code };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
    VkBool32 map_light_cull = (mode & 2) ? VK_TRUE : VK_FALSE;
    VkBool32 options[4] = { map_light_cull, (mode & 4) ? VK_TRUE : VK_FALSE, (mode & 8) ? VK_TRUE : VK_FALSE, (mode & 64) ? VK_TRUE : VK_FALSE };
    VkSpecializationMapEntry entries[4] = { { 0, 0, sizeof(VkBool32) },
        { 1, sizeof(VkBool32), sizeof(VkBool32) }, { 2, 2*sizeof(VkBool32), sizeof(VkBool32) },
        { 4, 3*sizeof(VkBool32), sizeof(VkBool32) } };
    VkSpecializationInfo specialization = { 4, entries, sizeof(options), options };
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = pipeline_statistics_flags(),
        .layout = pt.layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main",
            .pSpecializationInfo = &specialization } };
    // CPU compilation can dominate a cold, time-bounded test. Record it apart
    // from the GPU timestamps; cached selections never re-enter this path.
    ri.Printf(PRINT_ALL, "PT_LIGHTING_PIPELINE_BEGIN mode=%u\n", mode);
    int pipeline_started = ri.Milliseconds();
    VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.lighting_pipelines[mode]);
    ri.Printf(PRINT_ALL, "PT_LIGHTING_PIPELINE_END mode=%u cpu_ms=%d result=%d\n",
        mode, ri.Milliseconds() - pipeline_started, (int)result);
    qvkDestroyShaderModule(vk.device, module, NULL);
    if (result == VK_SUCCESS) {
        pipeline_statistics_print(pt.lighting_pipelines[mode], mode, pipeline.flags);
        return qtrue;
    }
    if (pt.lighting_pipelines[mode]) qvkDestroyPipeline(vk.device, pt.lighting_pipelines[mode], NULL);
    pt.lighting_pipelines[mode] = VK_NULL_HANDLE;
fail:
    pt.lighting_failed[mode] = qtrue;
    ri.Printf(PRINT_WARNING, "PT_LIGHTING_UNAVAILABLE mode=%u: pipeline creation failed; safe fallback retained\n", mode);
    return qfalse;
}

static qboolean lighting_material_cache_eligible(void)
{
    // Shader-clock diagnostics intentionally measure the original material program.
    return !r_pathTracingShaderProfile->integer;
}

static uint32_t lighting_pipeline_requested(void)
{
    if (pt.software) return 0; // No hardware-only specialized pipelines.
    return (r_pathTracingBRDFReuse->integer ? 1u : 0u) |
        (r_pathTracingMapLightCull->integer ? 2u : 0u) |
        (r_pathTracingAliasPDF->integer ? 4u : 0u) |
        (r_pathTracingEmitterGeometry->integer ? 8u : 0u) |
        (r_pathTracingLightLoop->integer ? 16u : 0u) |
        (r_pathTracingMaterialCache->integer && lighting_material_cache_eligible() ? 32u : 0u) |
        (r_pathTracingDlightReservoir->integer ? 64u : 0u);
}

static qboolean lighting_pipeline_select(uint32_t requested)
{
    if (requested >= ARRAY_LEN(pt.lighting_pipelines)) return qfalse;
    // Do not compile an unused reference integrator at normal startup.
    if (!pt.software && compact_transport_select(requested)) {
        pt.lighting_mode = requested;
        return qtrue;
    }
    if (lighting_pipeline_initialize(requested)) {
        pt.lighting_mode = requested;
        return qtrue;
    }
    if (requested & 64) return lighting_pipeline_select(requested & 63);
    if (requested & 32) return lighting_pipeline_select(requested & 31);
    if (requested & 16) return lighting_pipeline_select(requested & 15);
    if (requested & 8) return lighting_pipeline_select(requested & 7);
    // Remove only the failed alias candidate, retaining the requested older mode.
    if (requested & 4) return lighting_pipeline_select(requested & 3);
    // An optional combined candidate must not discard proven BRDF reuse.
    if (requested == 3 && lighting_pipeline_initialize(1)) {
        pt.lighting_mode = 1;
        return qtrue;
    }
    if (requested != 0 && lighting_pipeline_initialize(0)) {
        pt.lighting_mode = 0;
        return qtrue;
    }
    // A failed debug-mode switch must never bind a null handle. During initial
    // setup no pipeline exists yet, so failure propagates to normal cleanup.
    return (pt.lighting_pipelines[pt.lighting_mode] != VK_NULL_HANDLE &&
        (!(pt.lighting_mode & 32) || lighting_material_cache_eligible())) ||
        compact_transport_select(pt.lighting_mode);
}

static void lighting_pipeline_shutdown(void)
{
    for (uint32_t mode = 0; mode < ARRAY_LEN(pt.lighting_pipelines); ++mode) {
        if (pt.lighting_pipelines[mode]) qvkDestroyPipeline(vk.device, pt.lighting_pipelines[mode], NULL);
        pt.lighting_pipelines[mode] = VK_NULL_HANDLE;
        pt.lighting_failed[mode] = qfalse;
    }
    pt.lighting_mode = 0;
}

void vk_pt_profile(VkCommandBuffer cmd, uint32_t point)
{
    gpu_label_point(cmd, point);
    if (!point && pt_gpu_labels.name) {
        gpu_label_pipeline(0, pt.compact_transport_pipeline[(pt.lighting_mode & 4) ? 1 : 0], "VQ3E Path tracing - compact transport");
        gpu_label_pipeline(1, pt.guide_pipeline, "VQ3E Surface guides");
        gpu_label_pipeline(2, pt.material_cache_pipeline, "VQ3E Material preprocessing");
        gpu_label_pipeline(3, pt.temporal_pipeline, "VQ3E Temporal reconstruction");
        gpu_label_pipeline(4, pt.denoise_pipeline, "VQ3E Spatial reconstruction");
    }
    if (pt.active && point == 0) { shader_profile_read(); if(pt.software) sw_profile_read(); pt_exposure_read(); }
    if (!pt.active || !r_pathTracingProfile->integer) { pt.profile_mask = 0; return; }
    if (!pt.profile_pool) {
        VkPhysicalDeviceProperties properties;
        qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
        if (!properties.limits.timestampComputeAndGraphics) return;
        pt.timestamp_period = properties.limits.timestampPeriod;
        pt.timestamp_mask = UINT64_MAX;
        uint32_t family_count = 0;
        qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &family_count, NULL);
        VkQueueFamilyProperties *families = malloc(family_count*sizeof(*families));
        if (!families) return;
        qvkGetPhysicalDeviceQueueFamilyProperties(vk.physical_device, &family_count, families);
        uint32_t valid_bits = families[vk.queue_family_index].timestampValidBits;
        free(families);
        if (!valid_bits) return;
        if (valid_bits < 64) pt.timestamp_mask = (1ULL << valid_bits)-1;
        PFN_vkCreateQueryPool create = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr(vk.device, "vkCreateQueryPool");
        pt.destroy_queries = (PFN_vkDestroyQueryPool)qvkGetDeviceProcAddr(vk.device, "vkDestroyQueryPool");
        pt.get_queries = (PFN_vkGetQueryPoolResults)qvkGetDeviceProcAddr(vk.device, "vkGetQueryPoolResults");
        pt.reset_queries = (PFN_vkCmdResetQueryPool)qvkGetDeviceProcAddr(vk.device, "vkCmdResetQueryPool");
        pt.write_timestamp = (PFN_vkCmdWriteTimestamp)qvkGetDeviceProcAddr(vk.device, "vkCmdWriteTimestamp");
        VkQueryPoolCreateInfo info = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = pt.software ? 13 : 8 };
        if (!create || !pt.destroy_queries || !pt.get_queries || !pt.reset_queries || !pt.write_timestamp ||
            create(vk.device, &info, NULL, &pt.profile_pool) != VK_SUCCESS) return;
    }
    if (point == 0) {
        int now = ri.Milliseconds();
        uint64_t ticks[13];
        uint32_t query_count = pt.software ? 13 : 8;
        /* The existing render fence has completed. Never introduce a profiling
         * wait: unavailable/incomplete query sets are simply discarded. */
        if (pt.profile_mask == ((1u << query_count)-1u) && pt.get_queries(vk.device, pt.profile_pool, 0, query_count,
            query_count*sizeof(uint64_t), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            if(pt.software) {
#define SW_MS(a,b) (((ticks[b]-ticks[a]) & pt.timestamp_mask)*pt.timestamp_period/1000000.0)
                ri.Printf(PRINT_ALL,"%s frame=%u wall=%.3f scene_cpu=%.3f upload_bytes=%llu raster=%.3f upload=%.3f guides=%.3f trace=%.3f exposure=%.3f native=%.3f nrd=%.3f spatial=%.3f post=%.3f upscale=%.3f ui=%.3f gpu=%.3f\n",
                    sw_profile.recorded ? "SW_PROFILE_DIAGNOSTIC":"SW_PROFILE",
                    pt.frame, vk_pt_software_clock()-pt.software_profile_time, pt.software_scene_cpu,
                    (unsigned long long)vk_rt_scene_upload_bytes(), SW_MS(0,1),SW_MS(1,2),SW_MS(2,3),SW_MS(3,4),
                    SW_MS(4,8),SW_MS(8,9),SW_MS(9,10),SW_MS(5,6),SW_MS(6,11),SW_MS(11,12),SW_MS(12,7),SW_MS(0,7));
#undef SW_MS
            } else {
            double ms[7];
            for (int i = 0; i < 7; ++i) ms[i] = ((ticks[i+1]-ticks[i]) & pt.timestamp_mask)*pt.timestamp_period/1000000.0;
            // The direct RR writer has no packing/filter dispatch here.
            // Report no work instead of subtracting adjacent empty intervals.
            if (pt_rr.direct_profile_frame) ms[4] = ms[5] = 0;
            ri.Printf(PRINT_ALL, "%s frame=%d raster=%.3f as=%.3f guides=%.3f trace=%.3f temporal=%.3f spatial=%.3f post=%.3f\n",
                (pt.profile_instrumented || pt.staged.profile_written) ? "PT_PROFILE_DIAGNOSTIC" : "PT_PROFILE",
                now-pt.profile_time, ms[0], ms[1], ms[2], ms[3], ms[4], ms[5], ms[6]);
            }
        }
        if(pt.software) pt.software_profile_time=vk_pt_software_clock();
        pt.profile_time = now;
        pt_rr.direct_profile_frame = qfalse;
        pt.profile_mask = 0;
        pt.reset_queries(cmd, pt.profile_pool, 0, query_count);
    }
    if (point > (pt.software ? 12u : 7u) || (point && !(pt.profile_mask & 1))) return;
    pt.write_timestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pt.profile_pool, point);
    pt.profile_mask |= 1u << point;
}

double vk_pt_software_clock(void)
{
    if(!r_pathTracingProfile->integer) return 0;
#ifdef _WIN32
    static LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    if(!frequency.QuadPart) QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart*1000.0/(double)frequency.QuadPart;
#else
    return ri.Milliseconds();
#endif
}

void vk_pt_software_scene_time(double start)
{
    if(pt.software && r_pathTracingProfile->integer) pt.software_scene_cpu=vk_pt_software_clock()-start;
}

static qboolean buffer_create(pt_buffer_t *b, VkDeviceSize size, qboolean mapped)
{
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryRequirements req;
    VkMemoryAllocateInfo alloc = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    info.size = size;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | (mapped ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (qvkCreateBuffer(vk.device, &info, NULL, &b->buffer) != VK_SUCCESS) return qfalse;
    qvkGetBufferMemoryRequirements(vk.device, b->buffer, &req);
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (qvkAllocateMemory(vk.device, &alloc, NULL, &b->memory) != VK_SUCCESS) return qfalse;
    if (qvkBindBufferMemory(vk.device, b->buffer, b->memory, 0) != VK_SUCCESS) return qfalse;
    b->size = size;
    if (mapped) {
        // CPU assembly/hash/partition reads require normal cached RAM. The
        // upload allocation may be write-combined, and is only written by memcpy.
        b->mapped = calloc(1, (size_t)size);
        if (!b->mapped) return qfalse;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (qvkCreateBuffer(vk.device, &info, NULL, &b->upload) != VK_SUCCESS) return qfalse;
        qvkGetBufferMemoryRequirements(vk.device, b->upload, &req);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (qvkAllocateMemory(vk.device, &alloc, NULL, &b->upload_memory) != VK_SUCCESS ||
            qvkBindBufferMemory(vk.device, b->upload, b->upload_memory, 0) != VK_SUCCESS ||
            qvkMapMemory(vk.device, b->upload_memory, 0, size, 0, &b->upload_mapped) != VK_SUCCESS) return qfalse;
    }
    return qtrue;
}

static void buffer_destroy(pt_buffer_t *b)
{
    free(b->mapped);
    if (b->upload_mapped) qvkUnmapMemory(vk.device, b->upload_memory);
    if (b->upload) qvkDestroyBuffer(vk.device, b->upload, NULL);
    if (b->upload_memory) qvkFreeMemory(vk.device, b->upload_memory, NULL);
    if (b->buffer) qvkDestroyBuffer(vk.device, b->buffer, NULL);
    if (b->memory) qvkFreeMemory(vk.device, b->memory, NULL);
    memset(b, 0, sizeof(*b));
}

#include "pt_staged.h"

static void bind_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize size)
{
    VkDescriptorBufferInfo info = { buffer, 0, size };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = pt.set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &info;
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
}

static void buffer_upload_range(VkCommandBuffer cmd, pt_buffer_t *b,
    VkDeviceSize offset, VkDeviceSize size)
{
    VkBufferCopy copy = {offset, offset, size};
    if (offset > b->size || size > b->size-offset) {
        ri.Error(ERR_DROP, "Path tracing upload range exceeds buffer capacity");
        return;
    }
    if (!size) return;
    memcpy((byte *)b->upload_mapped+offset, (byte *)b->mapped+offset, (size_t)size);
    qvkCmdCopyBuffer(cmd, b->upload, b->buffer, 1, &copy);
}

static void buffer_upload(VkCommandBuffer cmd, pt_buffer_t *b, VkDeviceSize size)
{
    buffer_upload_range(cmd, b, 0, size);
}

#include "pt_fog.h"

static uint32_t texture_id(image_t *image)
{
    uint32_t i;
    if (!image) image = tr.whiteImage;
    for (i = 0; i < pt.texture_count; ++i)
        if (pt.textures[i] == image) return i;
    if (i == PT_MAX_TEXTURES)
        ri.Error(ERR_DROP, "Path tracing: texture capacity exceeded (%u)", PT_MAX_TEXTURES);
    pt.textures[pt.texture_count++] = image;
    pt.textures_dirty = qtrue;
    return i;
}

static void copy_wave(float *out, const waveForm_t *wave)
{
    out[0] = wave->base; out[1] = wave->amplitude;
    out[2] = wave->phase; out[3] = wave->frequency;
}

static qboolean layer_initialize(pt_layer_t *out, const shaderStage_t *stage,
    const textureBundle_t *bundle, const shader_t *shader)
{
    int i;
    qboolean animated = qfalse;
    qboolean texture_only = qtrue;
    memset(out, 0, sizeof(*out));
    out->params[0] = bundle ? MAX(bundle->numImageAnimations, 1) : 1;
    for (i = 0; i < 8; ++i)
        out->images[i / 4][i % 4] = (float)texture_id(bundle ?
            bundle->image[MIN(i, (int)out->params[0] - 1)] : tr.whiteImage);
    out->color[0] = out->color[1] = out->color[2] = out->color[3] = 1;
    if (!stage || !bundle) {
        out->vectors[0][3] = 1;
        for (i = 0; i < 4; ++i) out->vectors[1][i] = 1;
        return qfalse;
    }
    out->params[1] = bundle->imageAnimationSpeed;
    out->params[2] = bundle->tcGen;
    out->params[3] = bundle->numTexMods;
    out->meta[0] = stage->rgbGen;
    out->meta[1] = stage->alphaGen;
    out->meta[2] = shader->clampTime;
    out->meta[3] = shader->timeOffset;
    out->generators[0] = stage->rgbWave.func;
    out->generators[1] = stage->alphaWave.func;
    out->generators[2] = (stage->stateBits & GLS_ATEST_GT_0) ? 1 :
        ((stage->stateBits & GLS_ATEST_LT_80) ? 2 : ((stage->stateBits & GLS_ATEST_GE_80) ? 3 : 0));
    out->generators[3] = stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
    for (i = 0; i < 4; ++i) out->color[i] = stage->constantColor[i] / 255.0f;
    copy_wave(out->rgb_wave, &stage->rgbWave);
    copy_wave(out->alpha_wave, &stage->alphaWave);
    memcpy(out->vectors[0], bundle->tcGenVectors[0], sizeof(vec3_t));
    memcpy(out->vectors[1], bundle->tcGenVectors[1], sizeof(vec3_t));
    // Environment artwork changes with the observing ray even without a time
    // modifier. It cannot reuse fixed-albedo history as if it were immutable.
    animated = bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED || bundle->numImageAnimations > 1 ||
        stage->rgbGen == CGEN_WAVEFORM || stage->alphaGen == AGEN_WAVEFORM;
    for (i = 0; i < bundle->numTexMods && i < TR_MAX_TEXMODS; ++i) {
        const texModInfo_t *m = &bundle->texMods[i];
        pt_texmod_t *dst = &out->mods[i];
        dst->a[0] = m->type;
        dst->a[1] = m->matrix[0][0]; dst->a[2] = m->matrix[1][0]; dst->a[3] = m->translate[0];
        dst->b[0] = m->matrix[0][1]; dst->b[1] = m->matrix[1][1]; dst->b[2] = m->translate[1]; dst->b[3] = m->wave.func;
        copy_wave(dst->wave, &m->wave);
        if (m->type == TMOD_SCALE) { dst->a[1] = m->scale[0]; dst->a[2] = m->scale[1]; }
        if (m->type == TMOD_SCROLL) { dst->a[1] = m->scroll[0]; dst->a[2] = m->scroll[1]; }
        if (m->type == TMOD_ROTATE) dst->a[1] = m->rotateSpeed;
        if (m->type != TMOD_NONE && m->type != TMOD_SCALE && m->type != TMOD_TRANSFORM) animated = qtrue;
        if (m->type == TMOD_TURBULENT || m->type < TMOD_NONE || m->type > TMOD_ENTITY_TRANSLATE)
            texture_only = qfalse;
    }
    // Compile the common immutable texture/color stage. Vector coordinates are
    // unused with TCGEN_TEXTURE, so their spare lanes hold the fast-path data.
    if (bundle->numImageAnimations <= 1 && !bundle->numTexMods && bundle->tcGen == TCGEN_TEXTURE &&
        (stage->rgbGen == CGEN_IDENTITY || stage->rgbGen == CGEN_IDENTITY_LIGHTING ||
         stage->rgbGen == CGEN_LIGHTING_DIFFUSE || stage->rgbGen == CGEN_CONST) &&
        (stage->alphaGen == AGEN_IDENTITY || stage->alphaGen == AGEN_SKIP || stage->alphaGen == AGEN_CONST)) {
        out->vectors[0][3] = 1;
        for (i = 0; i < 3; ++i) out->vectors[1][i] = stage->rgbGen == CGEN_CONST ? out->color[i] : 1;
        out->vectors[1][3] = stage->alphaGen == AGEN_CONST ? out->color[3] : 1;
    } else if (texture_only && bundle->tcGen == TCGEN_TEXTURE &&
        (stage->rgbGen == CGEN_IDENTITY || stage->rgbGen == CGEN_IDENTITY_LIGHTING ||
         stage->rgbGen == CGEN_LIGHTING_DIFFUSE || stage->rgbGen == CGEN_CONST || stage->rgbGen == CGEN_WAVEFORM) &&
        (stage->alphaGen == AGEN_IDENTITY || stage->alphaGen == AGEN_SKIP ||
         stage->alphaGen == AGEN_CONST || stage->alphaGen == AGEN_WAVEFORM)) {
        // Animated UV-only stages still use the exact shared stage program,
        // but need only UVs/derivatives and entity shader-time/scroll metadata.
        // No world/model positions, normals or vertex colors are consumed.
        // 1 remains the immutable one-texture shortcut; 2 is NOT that shortcut.
        out->vectors[0][3] = 2;
    }
    return animated;
}

static void material_legacy_chrome(pt_material_t *m, const shader_t *shader,
    const textureBundle_t *base_bundle)
{
    // Stock chrome_metal (including Q3DM0's starting-room pillars) is two
    // painted reflection pictures, not a light-emitting detail coat. Replace
    // those pictures with the existing uniform-pigment metal program. Both
    // radiance and RR guides then use the same physical, scene-traced BRDF.
    // Do not reinterpret emissive/PBR overrides, deformed effects or overlays.
    if (Q_stricmp(shader->name, "textures/base_wall/chrome_metal") ||
        shader->rtMaterialDefined || shader->rtBaseColorImage || shader->rtORMImage ||
        shader->rtNormalImage || shader->rtDielectricDefined || shader->numDeforms ||
        shader->rtSurfaceLight > 0 || shader->rtLightImage ||
        !base_bundle || base_bundle->tcGen != TCGEN_ENVIRONMENT_MAPPED ||
        m->params[0] != 0 || m->surface[3] != 0 || m->composition[1] != 0 ||
        m->composition[3] != 0 || m->composition[0] > 2) return;
    m->layers[0].vectors[0][3] = 3;
    m->surface[1] = 0.08f;
    m->surface[2] = 1;
    m->composition[0] = 1;
    m->composition[2] = 0;
    m->params[2] = 0;
    m->params[3] = 0;
    m->emission[1] = 0;
}

static uint32_t material_id(shader_t *shader)
{
    uint32_t id;
    int s, b;
    shaderStage_t *stage = NULL;
    textureBundle_t *base_bundle = NULL, *emissive_bundle = NULL;
    qboolean has_surface_base = qfalse;
    qboolean independent_glow = qfalse;
    image_t *base = NULL;
    pt_material_t *m;
    if (shader->remappedShader) shader = shader->remappedShader;
    id = (uint32_t)shader->index;
    if (id >= MAX_SHADERS) ri.Error(ERR_DROP, "Path tracing: invalid material index");
    if (pt.material_shaders[id] == shader) return id;
    pt.material_shaders[id] = shader;
    pt.material_count = MAX(pt.material_count, id+1);
    pt.materials_dirty = qtrue;
    m = (pt_material_t *)pt.materials.mapped + id;
    memset(m, 0, sizeof(*m));
    m->composition[1] = -1;
    // An environment-mapped opaque stage can be the ONLY color source on a
    // pickup. Conversely a purely additive environment shader is a glow shell,
    // not an opaque fallback and not a reflective coat over another base.
    for (s = 0; s < shader->numUnfoggedPasses; ++s) {
        const shaderStage_t *candidate = shader->stages[s];
        if (!candidate) continue;
        unsigned blend = candidate->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
        qboolean additive = (blend & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_ONE &&
            (blend & GLS_SRCBLEND_BITS) != GLS_SRCBLEND_ZERO;
        for (b = 0; b < NUM_TEXTURE_BUNDLES; ++b)
            if (!additive && !candidate->bundle[b].isLightmap && candidate->bundle[b].image[0])
                has_surface_base = qtrue;
    }
    for (s = 0; s < shader->numUnfoggedPasses; ++s) {
        shaderStage_t *candidate = shader->stages[s];
        if (!candidate) continue;
        for (b = 0; b < NUM_TEXTURE_BUNDLES; ++b) {
            if (!candidate->bundle[b].isLightmap && candidate->bundle[b].image[0]) {
                textureBundle_t *bundle = &candidate->bundle[b];
                unsigned blend = candidate->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
                /* Only stage zero can be collapsed by CollapseMultitexture.
                 * Its second non-lightmap bundle is a texture combine, not a
                 * second application of the surviving framebuffer blend. */
                if (s == 0 && b == 1 && !candidate->bundle[0].isLightmap) {
                    blend = shader->multitextureEnv == GL_ADD ?
                        (GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE) :
                        (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
                }
                qboolean additive = (blend & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_ONE &&
                    (blend & GLS_SRCBLEND_BITS) != GLS_SRCBLEND_ZERO;
                // Retain color-bearing environment bases and unbacked additive
                // shells. Deformed shaders also use environment coordinates
                // for authored glow (e.g. a scrolling hologram), not a static
                // reflective coat. Keep those stages, consistent with the
                // deformed-aura rule below. Ordinary solid reflective coats
                // and dielectric reflection maps remain BRDF-owned.
                if (bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED &&
                    ((additive && has_surface_base && !shader->numDeforms) || shader->rtDielectricDefined ||
                     (shader->contentFlags & CONTENTS_WATER) ||
                     (strstr(shader->name, "glass") && (shader->contentFlags & CONTENTS_TRANSLUCENT)))) continue;
                int layer = (int)m->composition[0];
                if (layer >= ARRAY_LEN(m->layers)) {
                    ri.Error(ERR_DROP, "Path tracing: too many material layers in %s", shader->name);
                    return id;
                }
                if (layer_initialize(&m->layers[layer], candidate, bundle, shader)) m->params[3] = 1;
                m->layers[layer].generators[3] = blend | (candidate->stateBits & GLS_DEPTHFUNC_EQUAL);
                m->composition[0] = layer+1;
                if (additive) {
                    if (shader->numDeforms && bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED &&
                        !(candidate->stateBits & GLS_DEPTHFUNC_EQUAL)) independent_glow = qtrue;
                    emissive_bundle = bundle;
                    m->composition[2] = (uint32_t)m->composition[2] | (1u << layer);
                    ++m->params[2];
                } else if (!base_bundle) {
                    base_bundle = bundle; stage = candidate; base = bundle->image[0];
                    m->composition[1] = layer;
                } else {
                    ++m->composition[3];
                }
            }
        }
    }
    m->surface[0] = (float)texture_id(base);
    m->surface[1] = shader->rtMaterialDefined ? shader->rtRoughness : 0.6f;
    m->surface[2] = shader->rtMaterialDefined ? shader->rtMetallic : 0.0f;
    if (stage) {
        if (stage->stateBits & GLS_ATEST_GT_0) m->surface[3] = 1;
        if (stage->stateBits & GLS_ATEST_LT_80) m->surface[3] = 2;
        if (stage->stateBits & GLS_ATEST_GE_80) m->surface[3] = 3;
    }
    m->emission[0] = (float)texture_id(shader->rtLightImage ? shader->rtLightImage : base);
    m->emission[3] = shader->rtLightImage != NULL;
    /* q3map radiometric values use map-compiler units. Keep this conversion
     * explicit; exposure is independent and never derived from a lightmap. */
    m->emission[1] = shader->rtSurfaceLight / 1000.0f;
    m->emission[2] = shader->isSky ? 1.0f : (shader->surfaceFlags & SURF_SKY ? 2.0f : 0.0f);
    m->params[1] = base_bundle != NULL;
    for (s = 0; s < 6; ++s) m->skybox[s/4][s%4] = -1;
    if (shader->isSky) {
        for (s = 0; s < 6; ++s) {
            image_t *face = shader->sky.outerbox[s];
            if (face && face != tr.defaultImage) m->skybox[s/4][s%4] = texture_id(face);
        }
        m->skybox[1][2] = shader->sky.cloudHeight;
        m->skybox[1][3] = m->composition[0]; // Before the no-stage white fallback.
    }
    if (m->composition[0] == 0) {
        layer_initialize(&m->layers[0], NULL, NULL, shader);
        m->composition[0] = 1;
        m->composition[1] = 0;
    }
    if (emissive_bundle && m->emission[1] <= 0) m->emission[1] = 1;
    if (!base_bundle && emissive_bundle) m->params[0] = 2; // additive, no opaque backing
    else if (stage && shader->sort > SS_OPAQUE) {
        unsigned blend = stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
        if (blend == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)) m->params[0] = 1;
        else if (blend == (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO) ||
                 blend == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR)) m->params[0] = 3;
        else if (blend == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR)) {
            m->params[0] = 3;
            m->absorption[3] = 1; // Complementary source-color filter (burn/impact marks).
        }
    }
    /* Native legacy classification. Coverage sprites and additive effects stay
     * coverage/additive; only actual water contents and glass shader surfaces
     * are dielectric. Explicit author properties always override defaults. */
    qboolean water = (shader->contentFlags & CONTENTS_WATER) != 0;
    qboolean glass = strstr(shader->name, "glass") != NULL &&
        (shader->contentFlags & CONTENTS_TRANSLUCENT) && m->surface[3] == 0;
    if (shader->rtDielectricDefined || water || glass) {
        m->params[0] = 4;
        m->surface[3] = 0;
        m->optical[0] = shader->rtDielectricDefined ? shader->rtIOR : (water ? 1.333f : 1.5f);
        m->optical[1] = shader->rtDielectricDefined ? shader->rtThickness : (water ? 0 : 2);
        m->optical[2] = water ? 1 : 0;
        m->surface[1] = shader->rtMaterialDefined ? shader->rtRoughness : (water ? 0.06f : 0.02f);
        m->surface[2] = 0;
        /* Old scrolling/environment stages describe water's appearance, not
         * emission or stochastic holes in the volume boundary. */
        m->emission[1] = 0;
        m->absorption[0] = water ? 0.004f : 0.001f;
        m->absorption[1] = water ? 0.0015f : 0.0003f;
        m->absorption[2] = water ? 0.0007f : 0.0002f;
    }
    if (!shader->rtMaterialDefined && !water && !glass) {
        if ((base_bundle && base_bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED) ||
            strstr(shader->name, "metal") || strstr(shader->name, "chrome")) {
            m->surface[1] = 0.3f; m->surface[2] = 0.85f;
        }
        if (strstr(shader->name, "stone") || strstr(shader->name, "concrete")) m->surface[1] = 0.85f;
    }
    for (s = 0; s < 4; ++s) m->maps[s] = -1;
    if (glass && base_bundle) m->maps[3] = 1; // colored/stained source glass
    if (shader->rtBaseColorImage) m->maps[0] = texture_id(shader->rtBaseColorImage);
    if (shader->rtNormalImage) m->maps[1] = texture_id(shader->rtNormalImage);
    if (shader->rtORMImage) m->maps[2] = texture_id(shader->rtORMImage);
    m->optical[3] = shader->rtNormalImage ? shader->rtNormalScale : 1;
    if (shader->rtAbsorptionDefined) VectorCopy(shader->rtAbsorption, m->absorption);
    // An animated cutout body can carry a glow stage that does not require
    // depth equality with the body. In its holes only the glow is present;
    // preserve transparent continuation there, not an opaque glowing patch.
    // Other color-overlay programs retain their existing coverage semantics.
    if (independent_glow && m->params[0] == 0 && m->surface[3] != 0 && m->composition[3] == 0)
        m->maps[3] = 3;
    // Health-cross bases use colored reflection artwork in place of a painted
    // diffuse texture. Keep its uniform pigment, but obtain the reflected image
    // from scene rays. Restrict this legacy conversion to health pickup bases;
    // ordinary environment artwork, authored PBR, alpha overlays and the separate
    // additive orb shells retain their own material rules.
    if (!strncmp(shader->name, "models/powerups/health/", sizeof("models/powerups/health/")-1) &&
        !shader->rtMaterialDefined && !shader->rtBaseColorImage && !shader->rtORMImage &&
        !shader->numDeforms && shader->rtSurfaceLight <= 0 && !shader->rtLightImage &&
        m->params[0] == 0 && m->surface[3] == 0 && m->composition[3] == 0 &&
        base_bundle && base_bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED) {
        int base_layer = (int)m->composition[1];
        m->layers[base_layer].vectors[0][3] = 3; // Uniform-pigment stage, not a UV program.
        m->surface[1] = 0.12f; m->surface[2] = 1;
        m->params[3] = 0;
        for (s = 0; s < (int)m->composition[0]; ++s) {
            const pt_layer_t *layer = &m->layers[s];
            if (layer->params[0] > 1 || layer->meta[0] == CGEN_WAVEFORM ||
                layer->meta[1] == AGEN_WAVEFORM) m->params[3] = 1;
            // The base's reflected-picture coordinates are discarded. Genuine
            // electrical/glow overlays still keep their original UV animation.
            if (s != base_layer && (layer->params[2] == TCGEN_ENVIRONMENT_MAPPED || layer->params[3] > 0))
                m->params[3] = 1;
        }
    }
    material_legacy_chrome(m, shader, base_bundle);
    // Pure environment-only additive geometry is a transparent reflective
    // envelope, not a lamp displaying a painted reflection image. Negative
    // optical thickness is the native zero-thickness sheet mode; no volume is
    // entered and transmission leaves in the incident direction. Vertex-deformed
    // power-up auras are authored animated glow, not reflective envelopes.
    if (m->params[0] == 2 && m->composition[0] > 0 && shader->numDeforms == 0 &&
        shader->rtSurfaceLight <= 0 && !shader->rtLightImage) {
        qboolean shell = qtrue;
        for (s = 0; s < (int)m->composition[0]; ++s)
            if (m->layers[s].params[2] != TCGEN_ENVIRONMENT_MAPPED) shell = qfalse;
        if (shell) {
            m->params[0] = 4;
            m->params[2] = 0;
            m->params[3] = 0;
            m->emission[1] = 0;
            m->surface[3] = 0;
            m->surface[1] = 0.02f; m->surface[2] = 0;
            m->optical[0] = 1.5f; m->optical[1] = -1; m->optical[2] = 0;
            // Spatial UV animation belongs to the discarded reflection picture.
            // Frame/color/alpha animation can still animate the uniform tint.
            for (s = 0; s < (int)m->composition[0]; ++s)
                if (m->layers[s].params[0] > 1 || m->layers[s].meta[0] == CGEN_WAVEFORM ||
                    m->layers[s].meta[1] == AGEN_WAVEFORM) m->params[3] = 1;
        }
    }
    if (shader->sort == SS_PORTAL) {
        // A portal texture coats the mirror/remote radiance, not an opaque wall.
        // maps.w=2 selects that layer composition in both transport and guides.
        m->maps[3] = 2;
        m->params[0] = 0;
        m->surface[3] = 0;
        m->optical[3] = shader->portalRange > 0 ? shader->portalRange : 256;
        if (!shader->rtMaterialDefined) {
            m->surface[1] = 0.02f;
            m->surface[2] = 1;
        }
    }
    if (m->emission[2] != 0) {
        // Cloud blend/alpha stages describe radiance at infinity, not holes or
        // transmissive geometry in the BSP sky boundary.
        m->params[0] = 0;
        m->surface[3] = 0;
    }
    if (m->params[0] != 0) ++pt.effect_materials;
    if (m->params[3] != 0) ++pt.animated_materials;
    return id;
}

static void load_map_lights(void)
{
    typedef struct {
        char classname[MAX_QPATH], target[MAX_QPATH], targetname[MAX_QPATH];
        vec3_t origin, color;
        float intensity, radius;
        int flags, style;
    } light_entity_t;
    light_entity_t *entities = ri.Malloc(MAX_MAP_ENTITIES * sizeof(*entities));
    int count = 0, i, j, legacy = 0;
    char *cursor = tr.world->entityString;
    const char *token;
    memset(entities, 0, MAX_MAP_ENTITIES * sizeof(*entities));
    pt.map_light_count = 0;
    while (*(token = R_ParseExt(&cursor, qtrue))) {
        light_entity_t *entity;
        if (strcmp(token, "{") || count == MAX_MAP_ENTITIES) {
            ri.Free(entities);
            ri.Error(ERR_DROP, "Path tracing: invalid map entity data");
            return;
        }
        entity = &entities[count++];
        VectorSet(entity->color, 1, 1, 1);
        entity->radius = 64;
        while (*(token = R_ParseExt(&cursor, qtrue)) && strcmp(token, "}")) {
            char key[MAX_TOKEN_CHARS];
            Q_strncpyz(key, token, sizeof(key));
            token = R_ParseExt(&cursor, qtrue);
            if (!Q_stricmp(key, "classname")) Q_strncpyz(entity->classname, token, sizeof(entity->classname));
            else if (!Q_stricmp(key, "target")) Q_strncpyz(entity->target, token, sizeof(entity->target));
            else if (!Q_stricmp(key, "targetname")) Q_strncpyz(entity->targetname, token, sizeof(entity->targetname));
            else if (!Q_stricmp(key, "origin")) sscanf(token, "%f %f %f", &entity->origin[0], &entity->origin[1], &entity->origin[2]);
            else if (!Q_stricmp(key, "_color")) sscanf(token, "%f %f %f", &entity->color[0], &entity->color[1], &entity->color[2]);
            else if (!Q_stricmp(key, "light") || !Q_stricmp(key, "_light")) entity->intensity = atof(token);
            else if (!Q_stricmp(key, "radius")) entity->radius = atof(token);
            else if (!Q_stricmp(key, "spawnflags")) entity->flags = atoi(token);
            else if (!Q_stricmp(key, "style") || !Q_stricmp(key, "_style")) entity->style = atoi(token);
        }
    }
    for (i = 0; i < count; ++i) {
        const light_entity_t *entity = &entities[i];
        uint32_t slot;
        float maximum;
        if (Q_strncmp(entity->classname, "light", 5)) continue;
        if (entity->intensity < 0) continue;
        if (pt.map_light_count == PT_MAX_MAP_LIGHTS) {
            ri.Free(entities);
            ri.Error(ERR_DROP, "Path tracing: map light capacity exceeded");
            return;
        }
        slot = pt.map_light_count++;
        memcpy(pt.map_positions[slot], entity->origin, sizeof(vec3_t));
        /* The original map compiler uses pointScale=7500. The path renderer
         * expresses map radiance in 1/1000 compiler units, like area emission. */
        pt.map_positions[slot][3] = (entity->intensity > 0 ? entity->intensity : 300) * 7.5f;
        maximum = fmaxf(entity->color[0], fmaxf(entity->color[1], entity->color[2]));
        for (j = 0; j < 3; ++j)
            pt.map_colors[slot][j] = maximum > 0 ? fmaxf(0, entity->color[j] / maximum) : 0;
        memset(pt.map_cones[slot], 0, sizeof(pt.map_cones[slot]));
        if (*entity->target) {
            for (j = 0; j < count; ++j) {
                vec3_t direction;
                float distance, radius;
                if (strcmp(entity->target, entities[j].targetname)) continue;
                VectorSubtract(entities[j].origin, entity->origin, direction);
                distance = VectorNormalize(direction);
                radius = (entity->radius > 0 ? entity->radius : 64) + 16;
                memcpy(pt.map_cones[slot], direction, sizeof(vec3_t));
                if (distance > 0)
                    pt.map_cones[slot][3] = distance / sqrtf(distance * distance + radius * radius);
                break;
            }
            if (j == count)
                ri.Printf(PRINT_WARNING, "Path tracing: light target %s missing; using a point light\n", entity->target);
        }
        if ((entity->flags & 1) || entity->style) ++legacy;
    }
    ri.Free(entities);
    ri.Printf(PRINT_ALL, "Path tracing: %u map point/spot lights loaded\n", pt.map_light_count);
    if (legacy)
        ri.Printf(PRINT_WARNING, "Path tracing: %d legacy styled/linear lights currently use steady inverse-square emission\n", legacy);
}

static void build_light_grid(void)
{
    pt_light_grid_t *grid = pt.light_grid.mapped;
    const bmodel_t *world = tr.world->bmodels;
    vec3_t extent;
    double scaled[PT_MAX_MAP_LIGHTS], weights[PT_MAX_MAP_LIGHTS];
    uint32_t small[PT_MAX_MAP_LIGHTS], large[PT_MAX_MAP_LIGHTS];
    uint32_t n = pt.map_light_count, cells = 1;
    VectorSubtract(world->bounds[1], world->bounds[0], extent);
    grid->origin_cell[3] = fmaxf(512, fmaxf(extent[0], fmaxf(extent[1], extent[2]))/8);
    for (uint32_t j = 0; j < 3; ++j) {
        grid->origin_cell[j] = world->bounds[0][j];
        grid->dimensions[j] = MAX(1, MIN(8, (uint32_t)ceilf(extent[j]/grid->origin_cell[3])));
        cells *= grid->dimensions[j];
    }
    grid->dimensions[3] = n;
    pt.light_cells = cells;
    for (uint32_t cell = 0; cell < cells && n; ++cell) {
        vec3_t center;
        uint32_t coordinate = cell, ns = 0, nl = 0;
        double sum = 0;
        for (uint32_t j = 0; j < 3; ++j) {
            center[j] = grid->origin_cell[j]+(coordinate%grid->dimensions[j]+0.5f)*grid->origin_cell[3];
            coordinate /= grid->dimensions[j];
        }
        for (uint32_t i = 0; i < n; ++i) {
            vec3_t delta;
            VectorSubtract(pt.map_positions[i], center, delta);
            double power = pt.map_positions[i][3]*(pt.map_colors[i][0]*0.2126+
                pt.map_colors[i][1]*0.7152+pt.map_colors[i][2]*0.0722);
            weights[i] = fmax(0, power)/fmax(DotProduct(delta, delta),
                grid->origin_cell[3]*grid->origin_cell[3]*0.25);
            sum += weights[i];
        }
        for (uint32_t i = 0; i < n; ++i) {
            float *entry = grid->entries[cell*n+i];
            // Twenty percent uniform probability is an explicit support floor.
            // No PVS/camera/occlusion decision can silently remove a light.
            entry[2] = sum > 0 ? 0.2/n+0.8*weights[i]/sum : 1.0/n;
            scaled[i] = entry[2]*n;
            if (scaled[i] < 1) small[ns++] = i; else large[nl++] = i;
        }
        while (ns && nl) {
            uint32_t s = small[--ns], l = large[--nl];
            grid->entries[cell*n+s][0] = scaled[s];
            grid->entries[cell*n+s][1] = l;
            scaled[l] += scaled[s]-1;
            if (scaled[l] < 1) small[ns++] = l; else large[nl++] = l;
        }
        while (ns) { uint32_t i = small[--ns]; grid->entries[cell*n+i][0] = 1; grid->entries[cell*n+i][1] = i; }
        while (nl) { uint32_t i = large[--nl]; grid->entries[cell*n+i][0] = 1; grid->entries[cell*n+i][1] = i; }
        cache_alias_probabilities(&grid->entries[cell*n], n);
    }
    pt.sampling_dirty = qtrue;
}

#include "pt_portal.h"

void vk_pt_begin_world(uint32_t vertices, uint32_t indices)
{
    int surface;
    if (!pt.active) return;
    // Renderer-owned scene caches must not exhaust the engine's small zone heap.
    free(pt.world_attributes);
    free(pt.world_materials);
    pt.world_attributes = calloc(vertices ? vertices : 1, sizeof(pt_vertex_t));
    pt.world_materials = calloc(indices ? indices / 3 : 1, sizeof(uint32_t));
    if (!pt.world_attributes || !pt.world_materials) {
        ri.Error(ERR_DROP, "Path tracing: unable to allocate world attribute cache");
        return;
    }
    pt.world_vertices = vertices;
    pt.world_indices = indices;
    pt.world_upload_pending = qtrue;
    memset(pt.material_shaders, 0, sizeof(pt.material_shaders));
    pt.texture_count = pt.history = 0;
    pt.material_count = 0;
    pt.sky_material = -1;
    pt.animated_materials = pt.effect_materials = 0;
    pt.frozen = qfalse;
    pt.logged = qfalse;
    pt.temporal_valid = qfalse;
    pt.motion_count[0] = pt.motion_count[1] = 0;
    memset(pt.motion_buckets, -1, sizeof(pt.motion_buckets));
    portal_load_world();
    load_map_lights();
    build_light_grid();
    VectorClear(pt.sun_color);
    VectorSet(pt.sun_direction, 0, 0, 1);
    qboolean sun_selected = qfalse;
    for (surface = 0; surface < tr.world->bmodels[0].numSurfaces; ++surface) {
        shader_t *shader = tr.world->bmodels[0].firstSurface[surface].shader;
        if (shader->remappedShader) shader = shader->remappedShader;
        if (!(shader->isSky || (shader->surfaceFlags & SURF_SKY))) continue;
        if (pt.sky_material < 0 || (shader->isSky && !pt.material_shaders[pt.sky_material]->isSky))
            pt.sky_material = material_id(shader);
        if (!sun_selected && shader->rtSunDefined) {
            VectorCopy(shader->rtSunColor, pt.sun_color);
            VectorCopy(shader->rtSunDirection, pt.sun_direction);
            sun_selected = qtrue;
        }
    }
}

void vk_pt_load_media(const byte *file, int length, const dheader_t *header)
{
    free(pt.media); free(pt.medium_planes);
    pt.media = NULL; pt.medium_planes = NULL; pt.medium_count = 0;
    if (!pt.active) return;
    const int lumps[4] = {LUMP_BRUSHES, LUMP_BRUSHSIDES, LUMP_PLANES, LUMP_MODELS};
    const int strides[4] = {sizeof(dbrush_t), sizeof(dbrushside_t), sizeof(dplane_t), sizeof(dmodel_t)};
    for (int i = 0; i < 4; ++i) {
        const lump_t *l = &header->lumps[lumps[i]];
        if (l->fileofs < 0 || l->filelen < 0 || l->fileofs > length ||
            l->filelen > length-l->fileofs || l->filelen % strides[i])
            ri.Error(ERR_DROP, "Path tracing: invalid medium BSP lump");
    }
    const dbrush_t *brushes = (const dbrush_t *)(file+header->lumps[LUMP_BRUSHES].fileofs);
    const dbrushside_t *sides = (const dbrushside_t *)(file+header->lumps[LUMP_BRUSHSIDES].fileofs);
    const dplane_t *planes = (const dplane_t *)(file+header->lumps[LUMP_PLANES].fileofs);
    uint32_t brushCount = header->lumps[LUMP_BRUSHES].filelen/sizeof(*brushes);
    uint32_t sideCount = header->lumps[LUMP_BRUSHSIDES].filelen/sizeof(*sides);
    uint32_t planeCount = header->lumps[LUMP_PLANES].filelen/sizeof(*planes), cursor = 0;
    if (header->lumps[LUMP_MODELS].filelen < sizeof(dmodel_t)) ri.Error(ERR_DROP, "Path tracing: missing medium world model");
    const dmodel_t *world = (const dmodel_t *)(file+header->lumps[LUMP_MODELS].fileofs);
    uint32_t firstBrush = LittleLong(world->firstBrush), worldBrushes = LittleLong(world->numBrushes);
    if (firstBrush > brushCount || worldBrushes > brushCount-firstBrush) ri.Error(ERR_DROP, "Path tracing: invalid medium world brushes");
    pt.media = calloc(MAX(brushCount, 1), sizeof(*pt.media));
    pt.medium_planes = calloc(MAX(sideCount, 1), sizeof(*pt.medium_planes));
    if (!pt.media || !pt.medium_planes) ri.Error(ERR_DROP, "Path tracing: cannot allocate water volumes");
    for (uint32_t i = firstBrush; i < firstBrush+worldBrushes; ++i) {
        uint32_t shaderIndex = LittleLong(brushes[i].shaderNum);
        if (shaderIndex >= tr.world->numShaders) ri.Error(ERR_DROP, "Path tracing: bad medium shader");
        if (!(tr.world->shaders[shaderIndex].contentFlags & CONTENTS_WATER)) continue;
        uint32_t first = LittleLong(brushes[i].firstSide), count = LittleLong(brushes[i].numSides);
        if (!count || first > sideCount || count > sideCount-first || cursor > sideCount-count)
            ri.Error(ERR_DROP, "Path tracing: bad medium brush sides");
        pt_medium_t *medium = &pt.media[pt.medium_count++];
        medium->first = cursor; medium->count = count;
        shader_t *shader = R_FindShader(tr.world->shaders[shaderIndex].shader, LIGHTMAP_NONE, qtrue);
        medium->ior = shader->rtDielectricDefined ? shader->rtIOR : 1.333f;
        VectorSet(medium->absorption, 0.004f, 0.0015f, 0.0007f);
        if (shader->rtAbsorptionDefined) VectorCopy(shader->rtAbsorption, medium->absorption);
        for (uint32_t j = 0; j < count; ++j) {
            uint32_t plane = LittleLong(sides[first+j].planeNum);
            if (plane >= planeCount) ri.Error(ERR_DROP, "Path tracing: bad medium plane");
            for (int axis = 0; axis < 3; ++axis) pt.medium_planes[cursor][axis] = LittleFloat(planes[plane].normal[axis]);
            pt.medium_planes[cursor++][3] = LittleFloat(planes[plane].dist);
        }
    }
    ri.Printf(PRINT_ALL, "Path tracing: %u BSP water volumes loaded for underwater transport\n", pt.medium_count);
    fog_load(brushes, brushCount, sides, sideCount, planes, planeCount);
}

static void camera_medium(const float *origin, float *out)
{
    out[0] = 1; out[1] = out[2] = out[3] = 0;
    for (uint32_t i = 0; i < pt.medium_count; ++i) {
        const pt_medium_t *medium = &pt.media[i];
        uint32_t j;
        for (j = 0; j < medium->count; ++j) {
            const float *plane = pt.medium_planes[medium->first+j];
            if (DotProduct(origin, plane) > plane[3]) break;
        }
        if (j != medium->count) continue;
        out[0] = medium->ior; VectorCopy(medium->absorption, out+1);
        break; // Overlapping BSP brushes describe the union of a water region.
    }
}

void vk_pt_world_vertex(uint32_t vertex, const float *normal, const float *uv, byte alpha)
{
    if (!pt.active) return;
    memcpy(pt.world_attributes[vertex].normal, normal, 3 * sizeof(float));
    memcpy(pt.world_attributes[vertex].uv, uv, 2 * sizeof(float));
    // RGB holds baked lighting on BSP surfaces, but alpha is independent
    // material data. The shared shader interpolates it for alphaGen vertex.
    for (int i = 0; i < 3; ++i) pt.world_attributes[vertex].color[i] = 1;
    pt.world_attributes[vertex].color[3] = alpha / 255.0f;
}

void vk_pt_world_surface(uint32_t first_index, uint32_t index_count, const msurface_t *surface)
{
    uint32_t i, id;
    if (!pt.active) return;
    id = material_id(surface->shader) | portal_world_flags(surface);
    for (i = first_index / 3; i < (first_index + index_count) / 3; ++i)
        pt.world_materials[i] = id;
}

uint32_t vk_pt_partition_world(uint32_t *indices, uint32_t count)
{
    uint32_t cursor = 0;
    for (int opaque_pass = 1; opaque_pass >= 0; --opaque_pass) {
        for (uint32_t i = 0; i < count/3; ++i) {
            const pt_material_t *m = (const pt_material_t *)pt.materials.mapped+(pt.world_materials[i]&0xffffu);
            qboolean opaque = m->params[0] == 0 && m->surface[3] == 0 && m->emission[2] == 0;
            if (opaque != opaque_pass) continue;
            memcpy(pt.partition_indices+cursor, indices+i*3, 3*sizeof(uint32_t));
            pt.partition_materials[cursor/3] = pt.world_materials[i];
            cursor += 3;
        }
        if (opaque_pass) pt.world_opaque_indices = cursor;
    }
    memcpy(indices, pt.partition_indices, count*sizeof(uint32_t));
    memcpy(pt.world_materials, pt.partition_materials, count/3*sizeof(uint32_t));
    return pt.world_opaque_indices;
}

static qboolean dynamic_opaque(uint32_t flags, const pt_material_t *m)
{
    /* Weapon instances are queried only by first-person rays, which ignore
     * third-person/no-shadow flags. Decals must always retain callbacks. */
    uint32_t excluded = (flags & 0x20000000u) ? 0x08000000u : 0xc8000000u;
    return !(flags & excluded) && m->params[0] == 0 &&
        m->surface[3] == 0 && m->emission[2] == 0;
}

void vk_pt_partition_dynamic(uint32_t *indices, uint32_t count, uint32_t starts[5])
{
    uint32_t cursor = 0;
    uint32_t *materials = pt.triangle_materials.mapped;
    qboolean enabled = r_pathTracingDynamicOpaque->integer != 0;
    // Stable partition changes triangle order only, never vertex correspondence.
    // World, regular opaque/callback, weapon opaque/callback. Separate BLAS
    // instances preserve existing shader primitive addressing without changes.
    starts[0] = 0;
    for (int bucket = 0; bucket < 4; ++bucket) {
        starts[bucket+1] = pt.world_indices + cursor;
        if (!enabled && (bucket&1) == 0) continue; // Preserve two scans when disabled.
        for (uint32_t i = pt.world_indices/3; i < count/3; ++i) {
            const pt_material_t *m = (const pt_material_t *)pt.materials.mapped + (materials[i]&0xffffu);
            int group = (materials[i]&0x20000000u) ? 2 : 0;
            if (!enabled || !dynamic_opaque(materials[i], m)) ++group;
            if (group != bucket) continue;
            memcpy(pt.partition_indices+cursor, indices+i*3, 3*sizeof(uint32_t));
            pt.partition_materials[cursor/3] = materials[i];
            cursor += 3;
        }
    }
    memcpy(indices+pt.world_indices, pt.partition_indices, cursor*sizeof(uint32_t));
    memcpy(materials+pt.world_indices/3, pt.partition_materials, cursor/3*sizeof(uint32_t));
}

/* Native materials are compiled into the renderer. The diagnostic assets and
 * geometry below exercise the real parser, upload, ray traversal and BSDF;
 * they are opt-in and never replace game assets or write a PK3. */
const char *vk_pt_builtin_shader(const char *name)
{
    static const struct { const char *name, *text; } definitions[] = {
        { "vqe/glass", "{\nrt_dielectric 1.5 2\nrt_absorption 0.001 0.0003 0.0002\n{ map $whiteimage }\n}" },
        { "vqe/water", "{\nsurfaceparm water\nrt_dielectric 1.333 0\n{ map $whiteimage }\n}" },
        { "vqe/metal", "{\nrt_material 0.25 1\n{ map $whiteimage }\n}" },
        { "vqe/test/checker", "{\nq3map_surfacelight 1500\n{ map *pt_checker }\n}" },
        { "vqe/test/light", "{\nq3map_surfacelight 8000\n{ map $whiteimage }\n}" },
        { "vqe/test/volume", "{\nrt_dielectric 1.5 0\nrt_absorption 0.003 0.0005 0.0002\n{ map $whiteimage }\n}" },
        { "vqe/test/pane", "{\nrt_dielectric 1.5 18\nrt_absorption 0.003 0.0005 0.0002\n{ map $whiteimage }\n}" },
        { "vqe/test/control", "{\nrt_dielectric 1 18\nrt_absorption 0 0 0\n{ map $whiteimage }\n}" },
        { "vqe/test/pbr", "{\nrt_material 0.5 0\nrt_basecolormap *pt_base\nrt_normalmap *pt_normal\nrt_ormmap *pt_orm\n{ map $whiteimage }\n}" },
        { "vqe/test/flat", "{\nrt_material 0.5 0\nrt_basecolormap *pt_base\nrt_normalmap *pt_normal\nrt_normalscale 0\nrt_ormmap *pt_orm\n{ map $whiteimage }\n}" },
        { "vqe/test/mirror", "{\nrt_material 0.02 1\n{ map $whiteimage }\n}" },
        { "vqe/test/moving", "{\nq3map_surfacelight 1500\n{ map *pt_base }\n}" },
        { "vqe/test/effect_backing", "{\nq3map_surfacelight 20\n{ map *pt_checker }\n}" },
        { "vqe/test/clear_add", "{\n{ map $whiteimage\nblendFunc add\nrgbGen const ( 0 0 0 ) }\n}" },
        { "vqe/test/clear_filter", "{\n{ map $whiteimage\nblendFunc filter }\n}" }
    };
    for (int i = 0; i < ARRAY_LEN(definitions); ++i)
        if (!Q_stricmp(name, definitions[i].name)) return definitions[i].text;
    return NULL;
}

static void test_quad(float *positions, uint32_t *indices, uint32_t *vc, uint32_t *ic,
    const vec3_t origin, const vec3_t u, const vec3_t v, const char *material)
{
    static const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    static const uint32_t triangles[6] = {0,1,2,0,2,3};
    vec3_t normal;
    CrossProduct(u, v, normal); VectorNormalize(normal);
    for (int i = 0; i < 4; ++i) {
        vec2_t uv = {(corners[i][0]+1)*0.5f, (corners[i][1]+1)*0.5f};
        for (int j = 0; j < 3; ++j) positions[(*vc+i)*3+j] = origin[j]+corners[i][0]*u[j]+corners[i][1]*v[j];
        vk_pt_world_vertex(*vc+i, normal, uv, 255);
    }
    for (int i = 0; i < 6; ++i) indices[*ic+i] = *vc+triangles[i];
    const msurface_t surface = { .shader = R_FindShader(material, LIGHTMAP_NONE, qtrue) };
    vk_pt_world_surface(*ic, 6, &surface);
    *vc += 4; *ic += 6;
}

void vk_pt_test_scene(float *positions, uint32_t *indices, uint32_t *vc, uint32_t *ic)
{
    byte checker[64*64*4], normal[64*64*4], orm[64*64*4], base[64*64*4];
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        int p = (y*64+x)*4, bright = ((x/4)^(y/4))&1;
        checker[p] = bright ? 220 : 15; checker[p+1] = bright ? 180 : 30; checker[p+2] = bright ? 60 : 180;
        vec3_t n = {0.55f*sinf(x*0.19635f), 0.55f*cosf(y*0.19635f), 1};
        VectorNormalize(n);
        for (int j = 0; j < 3; ++j) normal[p+j] = (byte)((n[j]*0.5f+0.5f)*255);
        orm[p] = 255; orm[p+1] = 20+x*3; orm[p+2] = y<32 ? 0 : 255;
        base[p] = 195; base[p+1] = 135; base[p+2] = 70;
        checker[p+3] = normal[p+3] = orm[p+3] = base[p+3] = 255;
    }
    R_CreateImage("*pt_checker", checker, 64, 64, qtrue, qfalse, GL_REPEAT);
    R_CreateImage("*pt_normal", normal, 64, 64, qtrue, qfalse, GL_REPEAT);
    R_CreateImage("*pt_orm", orm, 64, 64, qtrue, qfalse, GL_REPEAT);
    R_CreateImage("*pt_base", base, 64, 64, qtrue, qfalse, GL_REPEAT);
    if (r_pathTracingTestScene->integer == 3) {
        // Both effects are visually neutral. Their backing must stay visible,
        // even closer than the shadow bias/near plane and through intersections.
        test_quad(positions, indices, vc, ic, (vec3_t){10300,0,64}, (vec3_t){0,-280,0}, (vec3_t){0,0,155}, "vqe/test/effect_backing");
        test_quad(positions, indices, vc, ic, (vec3_t){10299.875f,130,64}, (vec3_t){0,-75,0}, (vec3_t){0,0,105}, "vqe/test/clear_add");
        test_quad(positions, indices, vc, ic, (vec3_t){10299.875f,-130,64}, (vec3_t){2,-75,0}, (vec3_t){0,0,105}, "vqe/test/clear_filter");
        pt.world_vertices = *vc; pt.world_indices = *ic;
        ri.Printf(PRINT_ALL, "Path tracing native effect fixture: 0.125-unit gap and intersecting neutral layers\n");
        return;
    }
    if (r_pathTracingTestScene->integer == 2) {
        test_quad(positions, indices, vc, ic, (vec3_t){10300,0,64}, (vec3_t){0,-210,0}, (vec3_t){0,0,125}, "vqe/test/mirror");
        test_quad(positions, indices, vc, ic, (vec3_t){9800,0,64}, (vec3_t){0,700,0}, (vec3_t){0,0,450}, "vqe/test/checker");
        material_id(R_FindShader("vqe/test/moving", LIGHTMAP_NONE, qtrue));
        pt.test_motion_valid = qfalse;
        pt.world_vertices = *vc; pt.world_indices = *ic;
        ri.Printf(PRINT_ALL, "Path tracing native mirror fixture: static plane and tracked moving reflected target\n");
        return;
    }
    // Camera: noclip; setviewpos 10000 0 64 0. Right on screen is -Y.
    test_quad(positions, indices, vc, ic, (vec3_t){10480,0,64}, (vec3_t){0,-300,0}, (vec3_t){0,0,185}, "vqe/test/checker");
    test_quad(positions, indices, vc, ic, (vec3_t){9970,0,220}, (vec3_t){0,95,0}, (vec3_t){0,0,70}, "vqe/test/light");
    test_quad(positions, indices, vc, ic, (vec3_t){10230,-145,120}, (vec3_t){24,-48,0}, (vec3_t){0,0,45}, "vqe/test/pane");
    test_quad(positions, indices, vc, ic, (vec3_t){10230,-40,120}, (vec3_t){24,-48,0}, (vec3_t){0,0,45}, "vqe/test/control");
    test_quad(positions, indices, vc, ic, (vec3_t){10230,70,120}, (vec3_t){0,-48,0}, (vec3_t){0,0,45}, "vqe/test/pbr");
    test_quad(positions, indices, vc, ic, (vec3_t){10230,175,120}, (vec3_t){0,-48,0}, (vec3_t){0,0,45}, "vqe/test/flat");
    // Closed glass volume with slanted side walls: all six boundary normals
    // point outward, so entering/exiting, attenuation and TIR are exercised.
    vec3_t center = {10270,-105,-4};
    vec3_t axes[3] = {{28,0,0},{25,75,0},{0,0,38}};
    for (int axis = 0; axis < 3; ++axis) for (int sign = -1; sign <= 1; sign += 2) {
        vec3_t origin, u, v;
        for (int j = 0; j < 3; ++j) {
            origin[j] = center[j]+sign*axes[axis][j];
            u[j] = axes[(axis+1)%3][j]*sign; v[j] = axes[(axis+2)%3][j];
        }
        test_quad(positions, indices, vc, ic, origin, u, v, "vqe/test/volume");
    }
    // Closed water volume uses the same path as stock CONTENTS_WATER surfaces.
    VectorSet(center, 10270,105,-4);
    for (int axis = 0; axis < 3; ++axis) for (int sign = -1; sign <= 1; sign += 2) {
        vec3_t origin, u, v;
        for (int j = 0; j < 3; ++j) {
            origin[j] = center[j]+sign*axes[axis][j];
            u[j] = axes[(axis+1)%3][j]*sign; v[j] = axes[(axis+2)%3][j];
        }
        test_quad(positions, indices, vc, ic, origin, u, v, "vqe/water");
    }
    ri.Printf(PRINT_ALL, "Path tracing native material fixture: glass pane, IOR=1 control, normal/ORM maps, glass and water volumes\n");
    pt.world_vertices = *vc;
    pt.world_indices = *ic;
}

void vk_pt_test_motion(float *positions, uint32_t *indices, uint32_t *vc, uint32_t *ic)
{
    if (!pt.active) return;
    if (r_pathTracingTestMotion->integer == 2) { pt.test_motion_valid = qfalse; return; }
    float y = r_pathTracingTestMotion->integer ? 120*sinf(backEnd.refdef.rd.time*0.0015f) : 0;
    static const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    static const uint32_t order[6] = {0,1,2,0,2,3};
    pt_vertex_t *vertices = pt.attributes.mapped;
    uint32_t *materials = pt.triangle_materials.mapped;
    uint32_t material = material_id(R_FindShader("vqe/test/moving", LIGHTMAP_NONE, qtrue));
    for (uint32_t i = 0; i < 4; ++i) {
        float *p = positions+(*vc+i)*3;
        VectorSet(p, 9900, y+corners[i][0]*45, 64+corners[i][1]*65);
        pt_vertex_t *v = vertices+*vc+i;
        memset(v, 0, sizeof(*v));
        VectorSet(v->normal, 1, 0, 0); v->normal[3] = 65534;
        v->uv[0] = (corners[i][0]+1)*0.5f; v->uv[1] = (corners[i][1]+1)*0.5f;
        VectorCopy(p, v->previous); v->previous[1] += pt.test_previous_y-y;
        v->previous[3] = pt.test_motion_valid ? 1 : 0;
        for (int j = 0; j < 4; ++j) v->color[j] = 1;
    }
    for (uint32_t i = 0; i < 6; ++i) indices[*ic+i] = *vc+order[i];
    materials[*ic/3] = materials[*ic/3+1] = material;
    *vc += 4; *ic += 6;
    pt.test_previous_y = y; pt.test_motion_valid = qtrue;
}

void vk_pt_begin_frame(void)
{
    if (!pt.active) return;
    pt.motion_count[pt.motion_frame] = pt.motion_vertices = 0;
    pt.motion_matched = pt.motion_rejected = 0;
    memset(pt.motion_buckets[pt.motion_frame], -1, sizeof(pt.motion_buckets[pt.motion_frame]));
    if (pt.world_upload_pending && pt.world_vertices) memcpy(pt.attributes.mapped, pt.world_attributes,
        pt.world_vertices * sizeof(pt_vertex_t));
    if (pt.world_upload_pending && pt.world_indices) memcpy(pt.triangle_materials.mapped, pt.world_materials,
        (pt.world_indices / 3) * sizeof(uint32_t));
}

static uint32_t entity_ray_flags(const trRefEntity_t *entity)
{
    const int renderfx = entity->e.renderfx;
    const qboolean brush = entity->e.reType == RT_MODEL &&
        R_GetModelByHandle(entity->e.hModel)->type == MOD_BRUSH;
    uint32_t flags = pt_weapon_flags(renderfx);
    if (renderfx & RF_THIRD_PERSON) flags |= 0x80000000u;
    // CG_Mover sets RF_NOSHADOW on BSP doors/lifts to suppress legacy stencil
    // shadows. It must not make the moving level geometry transparent to light.
    // Keep the flag for non-brush effects/models; material alpha/transmission
    // still decides which parts of a brush surface actually block a ray.
    if ((renderfx & RF_NOSHADOW) && !brush) flags |= 0x40000000u;
    if (renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)) flags |= 0x20000000u;
    return flags;
}

void vk_pt_capture(uint32_t first_vertex, uint32_t first_index,
    uint32_t vertex_count, uint32_t index_count, const float (*normals)[4],
    const float (*uv)[2][2], const float (*axis)[3], shader_t *shader,
    const float *world_positions, const uint32_t *local_indices)
{
    uint32_t i, k, id;
    pt_vertex_t *vertices;
    uint32_t *triangles;
    const pt_motion_surface_t *previous = NULL;
    const trRefEntity_t *entity = backEnd.currentEntity;
    uint32_t motion_id = entity->motionId ? entity->motionId : 65535u;
    if (!pt.active) return;
    const qboolean rigidBrush = entity->e.reType == RT_MODEL &&
        R_GetModelByHandle(entity->e.hModel)->type == MOD_BRUSH && !shader->numDeforms;
    id = material_id(shader);
    const pt_material_t *material = (const pt_material_t *)pt.materials.mapped + id;
    if (tess.rayDynamicPolys && shader->polygonOffset && material->params[2] == 0 &&
        material->emission[1] == 0 && (material->params[0] <= 1 || material->params[0] == 3))
        id |= 0x08000000u; // Attached world decal: shading layer, never a floating blocker.
    /* Legacy/untracked submissions never borrow another object's history. */
    id |= 0x10000000u;
    if (entity != &tr.worldEntity && entity->motionId &&
        pt.motion_count[pt.motion_frame] < PT_MAX_MOTION_SURFACES) {
        uint32_t frame = pt.motion_frame, old = frame ^ 1, ordinal = 0;
        uint32_t key[7] = { entity->motionId, entity->motionGeneration,
            entity->e.hModel, entity->e.customSkin, shader->index,
            entity->e.customShader, entity->e.renderfx & (RF_FIRST_PERSON | RF_THIRD_PERSON | RF_DEPTHHACK) };
        uint32_t bucket = hash_bytes(2166136261u, key, sizeof(key)) % PT_MOTION_BUCKETS;
        uint32_t topology = hash_bytes(2166136261u, local_indices, index_count * sizeof(uint32_t));
        int entry;
        pt_motion_surface_t *current;
        for (entry = pt.motion_buckets[frame][bucket]; entry >= 0;
             entry = pt.motion_surfaces[frame][entry].next)
            if (!memcmp(pt.motion_surfaces[frame][entry].key, key, sizeof(key))) ++ordinal;
        for (entry = pt.motion_buckets[old][bucket]; entry >= 0;
             entry = pt.motion_surfaces[old][entry].next) {
            const pt_motion_surface_t *candidate = &pt.motion_surfaces[old][entry];
            if (!memcmp(candidate->key, key, sizeof(key)) &&
                ((rigidBrush && candidate->rigidBrush) ||
                 (!rigidBrush && !candidate->rigidBrush && candidate->ordinal == ordinal &&
                  candidate->topology == topology && candidate->count == vertex_count))) {
                previous = candidate;
                break;
            }
        }
        if (previous) {
            for (i = 0; i < vertex_count; ++i) {
                vec3_t delta;
                vec3_t oldPosition;
                if (rigidBrush) {
                    // Brush batching/sort order is not vertex identity. A rigid
                    // BSP model's local point plus its prior pose is identity.
                    for (k = 0; k < 3; ++k)
                        oldPosition[k] = previous->origin[k] + tess.xyz[i][0]*previous->axis[0][k] +
                            tess.xyz[i][1]*previous->axis[1][k] + tess.xyz[i][2]*previous->axis[2][k];
                } else VectorCopy(pt.motion_positions[old][previous->first + i], oldPosition);
                VectorSubtract(world_positions + i * 3, oldPosition, delta);
                if (DotProduct(delta, delta) > 256.0f * 256.0f) { previous = NULL; break; }
            }
        }
        entry = pt.motion_count[frame]++;
        current = &pt.motion_surfaces[frame][entry];
        memcpy(current->key, key, sizeof(key));
        current->topology = topology;
        current->ordinal = ordinal;
        current->count = vertex_count;
        current->first = pt.motion_vertices;
        current->rigidBrush = rigidBrush;
        if (rigidBrush) {
            VectorCopy(entity->e.origin, current->origin);
            memcpy(current->axis, axis, sizeof(current->axis));
        }
        current->next = pt.motion_buckets[frame][bucket];
        pt.motion_buckets[frame][bucket] = entry;
        memcpy(pt.motion_positions[frame] + pt.motion_vertices, world_positions, vertex_count * sizeof(vec3_t));
        pt.motion_vertices += vertex_count;
    }
    if (previous) ++pt.motion_matched; else ++pt.motion_rejected;
    vertices = (pt_vertex_t *)pt.attributes.mapped + first_vertex;
	/* Low 16 bits are the material; high bits are per-object ray visibility. */
    id |= entity_ray_flags(entity);
    triangles = (uint32_t *)pt.triangle_materials.mapped + first_index / 3;
    for (i = 0; i < vertex_count; ++i) {
        memset(&vertices[i], 0, sizeof(vertices[i]));
        for (k = 0; k < 3; ++k)
            vertices[i].normal[k] = normals[i][0] * axis[0][k] +
                normals[i][1] * axis[1][k] + normals[i][2] * axis[2][k];
        memcpy(vertices[i].uv, uv[i][0], sizeof(float) * 2);
        vertices[i].normal[3] = (float)motion_id;
        vertices[i].meta[0] = entity->e.shaderTime;
        vertices[i].meta[1] = entity->e.shaderTexCoord[0];
        vertices[i].meta[2] = entity->e.shaderTexCoord[1];
        {
            uint32_t rgba = entity->e.shaderRGBA[0] | (entity->e.shaderRGBA[1] << 8) |
                (entity->e.shaderRGBA[2] << 16) | ((uint32_t)entity->e.shaderRGBA[3] << 24);
            memcpy(vertices[i].meta + 3, &rgba, sizeof(rgba));
        }
        for (k = 0; k < 4; ++k) vertices[i].color[k] = tess.vertexColors[i][k] / 255.0f;
        memcpy(vertices[i].local, tess.xyz[i], sizeof(vec3_t));
        vertices[i].local[3] = tess.rayDynamicPolys ? (float)tess.rayPolyIds[i] : 0;
        if (previous) {
            if (rigidBrush) {
                for (k = 0; k < 3; ++k)
                    vertices[i].previous[k] = previous->origin[k] + tess.xyz[i][0]*previous->axis[0][k] +
                        tess.xyz[i][1]*previous->axis[1][k] + tess.xyz[i][2]*previous->axis[2][k];
            } else memcpy(vertices[i].previous, pt.motion_positions[pt.motion_frame ^ 1][previous->first + i], sizeof(vec3_t));
            vertices[i].previous[3] = 1;
        }
    }
    for (i = 0; i < index_count / 3; ++i) {
        // A batch can contain tagged and untagged polygons with the same shader.
        unsigned flags = tess.rayDynamicPolys ? pt_weapon_flags(tess.rayPolyFlags[local_indices[i * 3]]) : 0;
        if (shader->sort == SS_PORTAL && entity == &tr.worldEntity)
            flags |= portal_triangle_flags(shader, world_positions+3*local_indices[i*3],
                world_positions+3*local_indices[i*3+1], world_positions+3*local_indices[i*3+2]);
        triangles[i] = id | flags;
    }
}

qboolean vk_pt_initialize(uint32_t width, uint32_t height,
    uint32_t max_vertices, uint32_t max_indices,
    VkImageView color, VkImageView depth, VkImageView output,
    VkImageView motion, VkImageView path_depth)
{
    gpu_labels_initialize();
    uint32_t i;
    VkDescriptorSetLayoutBinding bindings[53] = {0};
    pt.software = r_rayTracing->integer == 1;
    pt.width=width; pt.height=height;
    if(pt.software) sw_denoise_initialize(output);
    const uint32_t binding_count = pt.software ? (sw_denoise.instance ? 53:52) : 50;
    VkPhysicalDeviceProperties properties;
    VkDescriptorSetLayoutCreateInfo set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PT_MAX_TEXTURES + 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 43 }
    };
    VkDescriptorPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
    VkDescriptorSetAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkShaderModuleCreateInfo module_info = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    VkShaderModule module;
    VkSamplerCreateInfo sampler = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    VkDescriptorImageInfo images[3];
    VkWriteDescriptorSet writes[3] = {0};
    extern unsigned char pt_guides_comp_spv[];
    extern int pt_guides_comp_spv_size;
    extern unsigned char pt_denoise_comp_spv[];
    extern int pt_denoise_comp_spv_size;
    extern unsigned char pt_temporal_comp_spv[];
    extern int pt_temporal_comp_spv_size;
    extern unsigned char pt_software_temporal_comp_spv[];
    extern int pt_software_temporal_comp_spv_size;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    if ((VkDeviceSize)width * height * 48 > properties.limits.maxStorageBufferRange ||
        max_vertices * sizeof(pt_vertex_t) > properties.limits.maxStorageBufferRange ||
        MAX_SHADERS * sizeof(pt_material_t) > properties.limits.maxStorageBufferRange) {
        ri.Printf(PRINT_WARNING, "Path tracing: resolution/scene exceeds device storage-buffer range\n");
        return qfalse;
    }
    if (!buffer_create(&pt.attributes, max_vertices * sizeof(pt_vertex_t), qtrue) ||
        !buffer_create(&pt.light_grid, sizeof(pt_light_grid_t), qtrue) ||
        !buffer_create(&pt.blue_noise, sizeof(pt_blue_noise), qtrue) ||
        !buffer_create(&pt.triangle_materials, (max_indices / 3) * sizeof(uint32_t), qtrue) ||
        !buffer_create(&pt.materials, MAX_SHADERS * sizeof(pt_material_t), qtrue) ||
        !buffer_create(&pt.lights, sizeof(pt_lights_t), qtrue) ||
        !buffer_create(&pt.fog, sizeof(pt_fog_header_t), qtrue) ||
        !buffer_create(&pt.accumulation, (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.guides[0], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.guides[1], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.guides[2], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.filter[0], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.filter[1], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.previous_guides[0], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.previous_guides[1], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.history_color[0], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.history_color[1], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.temporal_params, sizeof(pt_temporal_params_t), qtrue) ||
        !buffer_create(&pt.motion_guides[0], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse) ||
        !buffer_create(&pt.motion_guides[1], (VkDeviceSize)width * height * sizeof(float) * 4, qfalse))
        goto fail;
    pt.motion_positions[0] = malloc(max_vertices * sizeof(vec3_t));
    pt.partition_indices = malloc(max_indices*sizeof(uint32_t));
    pt.partition_materials = malloc(max_indices/3*sizeof(uint32_t));
    if (!pt.partition_indices || !pt.partition_materials) goto fail;
    if (!buffer_create(&pt.transmission, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.transmission_guide, (VkDeviceSize)width * height * 32, qfalse) ||
        !buffer_create(&pt.previous_transmission_guide, (VkDeviceSize)width * height * 32, qfalse) ||
        !buffer_create(&pt.transmission_motion, (VkDeviceSize)width * height * 48, qfalse) ||
        !buffer_create(&pt.specular, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.reflection_guide, (VkDeviceSize)width * height * 32, qfalse) ||
        !buffer_create(&pt.previous_reflection_guide, (VkDeviceSize)width * height * 32, qfalse) ||
        !buffer_create(&pt.reflection_motion, (VkDeviceSize)width * height * 32, qfalse) ||
        !buffer_create(&pt.light_change, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.visible_emission, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.shading_guide, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.previous_shading_guide, (VkDeviceSize)width * height * 16, qfalse)) goto fail;
    for (i = 0; i < 2; ++i)
        if (!buffer_create(&pt.transmission_history[i], (VkDeviceSize)width * height * 16, qfalse) ||
            !buffer_create(&pt.spec_history[i], (VkDeviceSize)width * height * 16, qfalse) ||
            !buffer_create(&pt.moments[i], (VkDeviceSize)width * height * 16, qfalse) ||
            !buffer_create(&pt.spec_filter[i], (VkDeviceSize)width * height * 16, qfalse)) goto fail;
    if(sw_denoise.instance && !buffer_create(&pt.software_metadata,(VkDeviceSize)width*height*16,qfalse)) goto fail;
    pt.motion_positions[1] = malloc(max_vertices * sizeof(vec3_t));
    if (!pt.motion_positions[0] || !pt.motion_positions[1]) goto fail;
    memset(pt.motion_buckets, -1, sizeof(pt.motion_buckets));
    for (i = 0; i < binding_count; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = i == 9 ? PT_MAX_TEXTURES : 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    bindings[0].descriptorType = pt.software ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    if (pt.software) {
        sizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sizes[0].descriptorCount = sw_denoise.instance ? 4:3; // BVH + optional NRD metadata.
    }
    bindings[1].descriptorType = bindings[2].descriptorType =
        bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[24].descriptorType = bindings[25].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    set_info.bindingCount = binding_count;
    set_info.pBindings = bindings;
    VK_CHECK(qvkCreateDescriptorSetLayout(vk.device, &set_info, NULL, &pt.set_layout));
    pool.maxSets = 1;
    pool.poolSizeCount = ARRAY_LEN(sizes);
    pool.pPoolSizes = sizes;
    VK_CHECK(qvkCreateDescriptorPool(vk.device, &pool, NULL, &pt.pool));
    allocation.descriptorPool = pt.pool;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &pt.set_layout;
    VK_CHECK(qvkAllocateDescriptorSets(vk.device, &allocation, &pt.set));
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &pt.set_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    VK_CHECK(qvkCreatePipelineLayout(vk.device, &layout, NULL, &pt.layout));
    // Build the selected integrator once, before the first rendered map frame.
    // The original is an on-demand fallback, not an unused startup compile.
    if (!lighting_pipeline_select(lighting_pipeline_requested())) goto fail;
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.pName = "main";
    pipeline.layout = pt.layout;
    module_info.codeSize = pt_guides_comp_spv_size;
    module_info.pCode = (const uint32_t *)pt_guides_comp_spv;
    if (pt.software) {
        extern unsigned char pt_software_guides_comp_spv[];
        extern int pt_software_guides_comp_spv_size;
        module_info.codeSize = pt_software_guides_comp_spv_size;
        module_info.pCode = (const uint32_t *)pt_software_guides_comp_spv;
        if(sw_denoise.instance) {
            extern unsigned char pt_software_nrd_guides_comp_spv[];
            extern int pt_software_nrd_guides_comp_spv_size;
            module_info.codeSize=pt_software_nrd_guides_comp_spv_size;
            module_info.pCode=(const uint32_t *)pt_software_nrd_guides_comp_spv;
        }
    }
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.module = module;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.guide_pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    module_info.codeSize = pt_denoise_comp_spv_size;
    module_info.pCode = (const uint32_t *)pt_denoise_comp_spv;
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.module = module;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.denoise_pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    module_info.codeSize = pt.software ? pt_software_temporal_comp_spv_size : pt_temporal_comp_spv_size;
    module_info.pCode = (const uint32_t *)(pt.software ? pt_software_temporal_comp_spv : pt_temporal_comp_spv);
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.module = module;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.temporal_pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(qvkCreateSampler(vk.device, &sampler, NULL, &pt.sampler));
    images[0] = (VkDescriptorImageInfo){ pt.sampler, color, VK_IMAGE_LAYOUT_GENERAL };
    images[1] = (VkDescriptorImageInfo){ pt.sampler, depth, VK_IMAGE_LAYOUT_GENERAL };
    images[2] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, output, VK_IMAGE_LAYOUT_GENERAL };
    for (i = 0; i < 3; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = pt.set;
        writes[i].dstBinding = i + 1;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = bindings[i + 1].descriptorType;
        writes[i].pImageInfo = &images[i];
    }
    qvkUpdateDescriptorSets(vk.device, 3, writes, 0, NULL);
    images[0] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, motion, VK_IMAGE_LAYOUT_GENERAL };
    images[1] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, path_depth, VK_IMAGE_LAYOUT_GENERAL };
    for (i = 0; i < 2; ++i) {
        writes[i].dstBinding = 24 + i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &images[i];
    }
    qvkUpdateDescriptorSets(vk.device, 2, writes, 0, NULL);
    bind_buffer(6, pt.attributes.buffer, pt.attributes.size);
    bind_buffer(7, pt.triangle_materials.buffer, pt.triangle_materials.size);
    bind_buffer(8, pt.materials.buffer, pt.materials.size);
    bind_buffer(10, pt.lights.buffer, pt.lights.size);
    bind_buffer(11, pt.accumulation.buffer, pt.accumulation.size);
    for (i = 0; i < 3; ++i) bind_buffer(12 + i, pt.guides[i].buffer, pt.guides[i].size);
    bind_buffer(15, pt.filter[0].buffer, pt.filter[0].size);
    bind_buffer(16, pt.filter[1].buffer, pt.filter[1].size);
    bind_buffer(17, pt.temporal_params.buffer, pt.temporal_params.size);
    bind_buffer(18, pt.history_color[1].buffer, pt.history_color[1].size);
    bind_buffer(19, pt.previous_guides[0].buffer, pt.previous_guides[0].size);
    bind_buffer(20, pt.previous_guides[1].buffer, pt.previous_guides[1].size);
    bind_buffer(21, pt.history_color[0].buffer, pt.history_color[0].size);
    bind_buffer(22, pt.motion_guides[0].buffer, pt.motion_guides[0].size);
    bind_buffer(23, pt.motion_guides[1].buffer, pt.motion_guides[1].size);
    bind_buffer(26, pt.specular.buffer, pt.specular.size);
    bind_buffer(27, pt.visible_emission.buffer, pt.visible_emission.size);
    bind_buffer(30, pt.spec_filter[0].buffer, pt.spec_filter[0].size);
    bind_buffer(31, pt.spec_filter[1].buffer, pt.spec_filter[1].size);
    bind_buffer(34, pt.light_grid.buffer, pt.light_grid.size);
    bind_buffer(35, pt.blue_noise.buffer, pt.blue_noise.size);
    bind_buffer(38, pt.reflection_motion.buffer, pt.reflection_motion.size);
    bind_buffer(41, pt.light_change.buffer, pt.light_change.size);
    bind_buffer(42, pt.transmission.buffer, pt.transmission.size);
    bind_buffer(47, pt.transmission_motion.buffer, pt.transmission_motion.size);
    bind_buffer(48, pt.fog.buffer, pt.fog.size);
    pt.fog_dirty = qtrue;
    memcpy(pt.blue_noise.mapped, pt_blue_noise, sizeof(pt_blue_noise));
    pt.width = width;
    pt.height = height;
    pt.max_vertices = max_vertices;
    pt.max_indices = max_indices;
    pt.active = qtrue;
    if(sw_denoise.instance) bind_buffer(52,pt.software_metadata.buffer,pt.software_metadata.size);
    if(pt.software) sw_profile_initialize();
    ri.Printf(PRINT_ALL, "Experimental native path tracer initialized at %ux%u\n", width, height);
    return qtrue;
fail:
    vk_pt_shutdown();
    return qfalse;
}

void vk_pt_shutdown(void)
{
    sw_profile_shutdown();
    sw_denoise_shutdown();
    buffer_destroy(&pt.software_metadata);
    pt_exposure_shutdown();
    rr_shutdown();
    memset(&pt_gpu_labels, 0, sizeof(pt_gpu_labels));
    staged_shutdown();
    shader_profile_shutdown();
    free(pt.partition_indices); free(pt.partition_materials);
    if (pt.profile_pool) pt.destroy_queries(vk.device, pt.profile_pool, NULL);
    free(pt.media); free(pt.medium_planes);
    free(pt.world_attributes);
    free(pt.world_materials);
    buffer_destroy(&pt.attributes);
    buffer_destroy(&pt.light_grid);
    buffer_destroy(&pt.blue_noise);
    buffer_destroy(&pt.reflection_guide);
    buffer_destroy(&pt.previous_reflection_guide);
    buffer_destroy(&pt.reflection_motion);
    buffer_destroy(&pt.light_change);
    buffer_destroy(&pt.transmission);
    buffer_destroy(&pt.transmission_guide);
    buffer_destroy(&pt.previous_transmission_guide);
    buffer_destroy(&pt.transmission_motion);
    buffer_destroy(&pt.triangle_materials);
    buffer_destroy(&pt.materials);
    buffer_destroy(&pt.lights);
    buffer_destroy(&pt.fog);
    buffer_destroy(&pt.accumulation);
    buffer_destroy(&pt.guides[0]);
    buffer_destroy(&pt.guides[1]);
    buffer_destroy(&pt.guides[2]);
    buffer_destroy(&pt.filter[0]);
    buffer_destroy(&pt.filter[1]);
    buffer_destroy(&pt.previous_guides[0]);
    buffer_destroy(&pt.previous_guides[1]);
    buffer_destroy(&pt.history_color[0]);
    buffer_destroy(&pt.history_color[1]);
    buffer_destroy(&pt.temporal_params);
    buffer_destroy(&pt.motion_guides[0]);
    buffer_destroy(&pt.motion_guides[1]);
    buffer_destroy(&pt.specular);
    buffer_destroy(&pt.visible_emission);
    buffer_destroy(&pt.shading_guide);
    buffer_destroy(&pt.previous_shading_guide);
    for (int i = 0; i < 2; ++i) {
        buffer_destroy(&pt.transmission_history[i]);
        buffer_destroy(&pt.spec_history[i]);
        buffer_destroy(&pt.moments[i]);
        buffer_destroy(&pt.spec_filter[i]);
    }
    free(pt.motion_positions[0]);
    free(pt.motion_positions[1]);
    lighting_pipeline_shutdown();
    for (int i = 0; i < 2; ++i)
        if (pt.compact_transport_pipeline[i]) qvkDestroyPipeline(vk.device, pt.compact_transport_pipeline[i], NULL);
    if (pt.material_cache_pipeline) qvkDestroyPipeline(vk.device, pt.material_cache_pipeline, NULL);
    if (pt.guide_pipeline) qvkDestroyPipeline(vk.device, pt.guide_pipeline, NULL);
    if (pt.denoise_pipeline) qvkDestroyPipeline(vk.device, pt.denoise_pipeline, NULL);
    if (pt.temporal_pipeline) qvkDestroyPipeline(vk.device, pt.temporal_pipeline, NULL);
    if (pt.layout) qvkDestroyPipelineLayout(vk.device, pt.layout, NULL);
    if (pt.pool) qvkDestroyDescriptorPool(vk.device, pt.pool, NULL);
    if (pt.set_layout) qvkDestroyDescriptorSetLayout(vk.device, pt.set_layout, NULL);
    if (pt.sampler) qvkDestroySampler(vk.device, pt.sampler, NULL);
    memset(&pt, 0, sizeof(pt));
}

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t i;
    for (i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}

void vk_pt_software_scene(VkBuffer nodes, VkDeviceSize node_size,
    VkBuffer links, VkDeviceSize link_size, VkBuffer triangles, VkDeviceSize triangle_size)
{
    if (!pt.active || !pt.software) return;
    bind_buffer(0, nodes, node_size);
    bind_buffer(50, links, link_size);
    bind_buffer(51, triangles, triangle_size);
}

qboolean vk_pt_record(VkCommandBuffer cmd, VkAccelerationStructureKHR scene,
    VkBuffer positions, VkBuffer indices, const float *cpu_positions,
    const uint32_t *cpu_indices, uint32_t vertex_count, uint32_t index_count,
    const float *projection, const float *origin, const float *axis,
    const float *sun, float near_distance, float far_distance, float ray_distance,
    qboolean reset_history)
{
    uint32_t i, hash, light_hash, radiance_hash, reset_reasons;
    qboolean geometry_changed, lights_changed, temporal_enabled, reject_history;
    float c[32] = {0};
    pt_temporal_params_t *reconstruction = pt.temporal_params.mapped;
    pt_lights_t *lights = pt.lights.mapped;
    const float ambient = r_pathTracingAmbient->value;
    const float flash_brightness = R_MuzzleFlashScale(r_muzzleFlashBrightness->value);
    const float flash_light_scale = R_MuzzleFlashScale(r_muzzleFlashLightScale->value);
    const float rocket_brightness = R_MuzzleFlashScale(r_rocketBrightness->value);
    const float rocket_light_scale = R_MuzzleFlashScale(r_rocketLightScale->value);
    const float explosion_light_scale = R_MuzzleFlashScale(r_rocketExplosionLightScale->value);
    const float lightning_light_scale = R_MuzzleFlashScale(r_lightningGunLightScale->value);
    const qboolean rocket_changed = lights->rocket_emission[0] != rocket_brightness ||
        lights->rocket_emission[1] != rocket_light_scale ||
        lights->rocket_emission[2] != explosion_light_scale || lights->rocket_emission[3] != lightning_light_scale;
    const qboolean flash_changed =
        memcmp(&lights->emitter_search_control[2], &flash_brightness, sizeof(float)) ||
        memcmp(&lights->emitter_search_control[3], &flash_light_scale, sizeof(float));
    const qboolean ambient_changed = lights->sky_environment[1] != ambient;
    const float sun_angle = r_pathTracingSunAngle->value * (float)(M_PI / 360.0);
    const float sun_scale = r_pathTracingSunScale->value;
    const float light_radius = r_pathTracingLightRadius->value;
    const qboolean penumbra_changed = lights->sky_environment[2] != sun_angle ||
        lights->sky_environment[3] != light_radius;
    float previous_positions[PT_MAX_LIGHTS][4], previous_colors[PT_MAX_LIGHTS][4];
    uint32_t previous_count = lights->counts[1];
    memcpy(previous_positions, lights->positions, sizeof(previous_positions));
    memcpy(previous_colors, lights->colors, sizeof(previous_colors));
    const uint32_t *triangle_materials = pt.triangle_materials.mapped;
    const pt_material_t *materials = pt.materials.mapped;
    VkWriteDescriptorSetAccelerationStructureKHR as = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    if (!pt.active || !pt.world_vertices) return qfalse;
    if (!lighting_pipeline_select(lighting_pipeline_requested())) return qfalse;
    qboolean material_cache_active = r_pathTracingMaterialCache->integer && lighting_material_cache_eligible() &&
        (pt.lighting_mode & 32) && material_cache_initialize();
    if (material_cache_active != pt.material_cache_active) {
        // Upload the original records on either transition: no stale GPU cache
        // may survive a switch off. The next enabled dispatch repopulates it.
        pt.materials_dirty = qtrue;
        pt.material_cache_active = material_cache_active;
        ri.Printf(PRINT_ALL, "PT_MATERIAL_CACHE enabled=%d requested=%d\n", material_cache_active, r_pathTracingMaterialCache->integer);
    }
    qboolean staged_active = material_cache_active && r_pathTracingCompactTransport->integer &&
        !r_pathTracingShaderProfile->integer && pt.lighting_mode == 57 &&
        r_pathTracingStaged->integer && staged_initialize();
    if (staged_active != pt.staged.active) {
        pt.history = 0;
        pt.temporal_valid = qfalse;
        pt.staged.active = staged_active;
        ri.Printf(PRINT_ALL, "PT_STAGED enabled=%d requested=%d\n", staged_active, r_pathTracingStaged->integer);
    }
    if (pt.materials_dirty || pt.material_program_mode != r_pathTracingMaterialFastPath->integer) {
        // Diagnostic A/B switch changes only unused-input elimination, not
        // texture programs, light transport, random sequences or history.
        pt_material_t *programs = pt.materials.mapped;
        for (uint32_t material = 0; material < pt.material_count; ++material) if (pt.material_shaders[material])
            for (int layer = 0; layer < (int)programs[material].composition[0]; ++layer) {
                float *program = &programs[material].layers[layer].vectors[0][3];
                if (*program == 2 || *program == -2)
                    *program = r_pathTracingMaterialFastPath->integer ? 2 : -2;
            }
        pt.material_program_mode = r_pathTracingMaterialFastPath->integer;
        pt.materials_dirty = qtrue;
    }
    if (!r_pathTracingReference->integer || !pt.frozen) pt.material_time = backEnd.refdef.rd.time * 0.001f;
    (void)sun;
    if (pt.texture_mode_revision != r_textureMode->modificationCount) {
        pt.textures_dirty = qtrue;
        reset_history = qtrue;
        pt.history = 0;
        pt.texture_mode_revision = r_textureMode->modificationCount;
    }
    if (pt.textures_dirty) {
        VkDescriptorImageInfo textures[PT_MAX_TEXTURES];
        for (i = 0; i < PT_MAX_TEXTURES; ++i) {
            image_t *texture = i < pt.texture_count ? pt.textures[i] : tr.whiteImage;
            // Share the native filtering policy (including mip/no-mip,
            // clamp/repeat and the device-limited anisotropy setting).
            // The image subsystem owns these cached samplers, not PT.
            textures[i] = (VkDescriptorImageInfo){ vk_find_sampler(texture->mipmap, texture->wrapClampMode == GL_REPEAT),
                texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        }
        write.dstSet = pt.set;
        write.dstBinding = 9;
        write.descriptorCount = PT_MAX_TEXTURES;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = textures;
        qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
        pt.textures_dirty = qfalse;
    }
    if (!r_pathTracingReference->integer || !pt.frozen || flash_changed || rocket_changed) {
        pt.muzzle_flash_triangles = 0;
        pt.rocket_triangles = 0;
        pt.rocket_explosion_triangles = pt.lightning_gun_triangles = 0;
        memset(lights, 0, sizeof(*lights));
        lights->sky_environment[0] = pt.sky_material+1;
        lights->selection[1] = pt.world_opaque_indices/3;
        for (i = 0; i < index_count / 3; ++i) {
            vec3_t e1, e2, cross;
            float power = materials[triangle_materials[i] & 0xffffu].emission[1];
            // A remote view is not a local area emitter. Its coating and
            // remote radiance are evaluated when a ray actually enters it.
            if (triangle_materials[i] & PT_PORTAL_MASK) power = 0;
            if (triangle_materials[i] & 0x04000000u) {
                ++pt.muzzle_flash_triangles;
                power *= flash_light_scale;
            } else if (triangle_materials[i] & 0x02000000u) {
                ++pt.rocket_triangles;
                power *= rocket_light_scale;
            }
            if (triangle_materials[i] & 0x01000000u) {
                ++pt.rocket_explosion_triangles;
                power *= explosion_light_scale;
            }
            if (triangle_materials[i] & 0x00800000u) {
                ++pt.lightning_gun_triangles;
                power *= lightning_light_scale;
            }
            float emission_power = power;
            const float *a, *b, *c;
            if (triangle_materials[i] & 0x08000000u) {
                for (uint32_t v = 0; v < 3; ++v) {
                    const float *point = cpu_positions + cpu_indices[i * 3 + v] * 3;
                    if (lights->decal_bounds[0][3] == 0 && v == 0) {
                        VectorCopy(point, lights->decal_bounds[0]);
                        VectorCopy(point, lights->decal_bounds[1]);
                    } else AddPointToBounds(point, lights->decal_bounds[0], lights->decal_bounds[1]);
                }
                lights->decal_bounds[0][3] += 1;
                continue;
            }
            if (triangle_materials[i] & 0x20000000u) ++lights->counts[2];
            if (power <= 0 || materials[triangle_materials[i] & 0xffffu].emission[2] != 0) continue;
            a = cpu_positions + cpu_indices[i * 3] * 3;
            b = cpu_positions + cpu_indices[i * 3 + 1] * 3;
            c = cpu_positions + cpu_indices[i * 3 + 2] * 3;
            VectorSubtract(b, a, e1);
            VectorSubtract(c, a, e2);
            CrossProduct(e1, e2, cross);
            power *= VectorLength(cross) * 0.5f;
            if (power <= 0) continue;
            if (lights->counts[0] == PT_MAX_EMITTERS)
                ri.Error(ERR_DROP, "Path tracing: emitter capacity exceeded");
            /* Area * declared emission is an importance proxy, independent of
             * camera visibility. Both NEE and BSDF-hit MIS use this same PDF. */
            lights->selection[0] += power;
            cache_emitter_geometry(&lights->emitter_geometry[lights->counts[0]], a, b, c,
                emission_power);
            lights->emitters[lights->counts[0]].primitive = i;
            lights->emitters[lights->counts[0]++].cumulative_power = lights->selection[0];
        }
        if (r_pathTracingEmitterSearch->integer)
            build_emitter_search(lights->emitters, lights->counts[0], lights->selection[0], lights->emitter_search_ranges);
        lights->counts[1] = MIN(backEnd.refdef.num_dlights, PT_MAX_LIGHTS);
        for (i = 0; i < lights->counts[1]; ++i) {
            const dlight_t *light = &backEnd.refdef.dlights[i];
            memcpy(lights->positions[i], light->origin, sizeof(float) * 3);
            lights->positions[i][3] = light->radius;
            memcpy(lights->colors[i], light->color, sizeof(float) * 3);
        }
        lights->counts[3] = pt.map_light_count;
        memcpy(lights->positions + PT_MAX_LIGHTS, pt.map_positions, pt.map_light_count * sizeof(pt.map_positions[0]));
        memcpy(lights->colors + PT_MAX_LIGHTS, pt.map_colors, pt.map_light_count * sizeof(pt.map_colors[0]));
        memcpy(lights->cones + PT_MAX_LIGHTS, pt.map_cones, pt.map_light_count * sizeof(pt.map_cones[0]));
    }
    // A diagnostic toggle can enable the index after reference geometry froze.
    // Normal rendering with the candidate disabled does not build the index.
    if (r_pathTracingEmitterSearch->integer && r_pathTracingReference->integer && pt.frozen &&
        !lights->emitter_search_control[0])
        build_emitter_search(lights->emitters, lights->counts[0], lights->selection[0], lights->emitter_search_ranges);
    lights->emitter_search_control[0] = r_pathTracingEmitterSearch->integer != 0;
    lights->emitter_search_control[1] = PT_EMITTER_SEARCH_BUCKETS;
    // Reuse reserved lanes; controls remain live even in reference mode.
    memcpy(&lights->emitter_search_control[2], &flash_brightness, sizeof(float));
    memcpy(&lights->emitter_search_control[3], &flash_light_scale, sizeof(float));
    lights->rocket_emission[0] = rocket_brightness;
    lights->rocket_emission[1] = rocket_light_scale;
    lights->rocket_emission[2] = explosion_light_scale;
    lights->rocket_emission[3] = lightning_light_scale;
    if (!r_pathTracingReference->integer || !pt.frozen) portal_update(lights);
    pt.frozen = r_pathTracingReference->integer != 0;
    // Keep the live control outside the frozen-reference geometry/light build.
    // Reuse a reserved scalar in the existing light buffer, without changing ABI.
    lights->sky_environment[1] = ambient;
    lights->sky_environment[2] = sun_angle;
    lights->sky_environment[3] = light_radius;
    c[0] = origin[0]; c[1] = origin[1]; c[2] = origin[2]; c[3] = near_distance;
    for (i = 0; i < 3; ++i) {
        c[4+i] = axis[i]; c[8+i] = -axis[3+i]; c[12+i] = axis[6+i];
        c[16+i] = pt.sun_direction[i]; c[24+i] = pt.sun_color[i] * sun_scale / 100.0f;
    }
    c[7] = far_distance; c[11] = projection[0]; c[15] = projection[5];
    /* Exposure initialization writes binding 49 of pt.set. In software mode,
     * do it before any dispatch binds that set, including when enabled live. */
    if (pt.software && pt_exposure_enabled()) pt_exposure_initialize();
    pt.exposure = pt_exposure_current();
    c[19] = pt.exposure;
    c[20] = 0.02f; c[21] = ray_distance;
    c[22] = projection[8]; c[23] = projection[9];
    c[27] = r_pathTracingDenoise->integer && !r_pathTracingReference->integer ? 1.0f : 0.0f;
    c[28] = r_pathTracingSamples->integer; c[29] = r_pathTracingBounces->integer;
    /* Flood-proof PT twin of mode-1's sw_dbg: write the exact push block that
     * this frame's PT invocations consume to a FILE (Streamline cannot flood a
     * FILE the way it floods the console). Diff against sw_shadow_debug.log. */
    pt_push_debug_record(c,r_rayTracing->integer,r_pathTracingSamples->integer,vertex_count/3);
    hash = hash_bytes(2166136261u, cpu_positions + pt.world_vertices * 3,
        (vertex_count - pt.world_vertices) * 3 * sizeof(float));
    hash = hash_bytes(hash, cpu_indices + pt.world_indices,
        (index_count - pt.world_indices) * sizeof(uint32_t));
    hash = hash_bytes(hash, triangle_materials + pt.world_indices / 3,
        ((index_count - pt.world_indices) / 3) * sizeof(uint32_t));
    geometry_changed = hash != pt.previous_hash;
    lights->selection[2] = lights->selection[3] = 0;
    light_hash = hash_bytes(2166136261u, lights, offsetof(pt_lights_t, previous_positions));
    lights_changed = light_hash != pt.previous_lights_hash;
    /* The emitter CDF contains animated triangle indices/areas and visibility
     * counts. Changes there are geometry motion, not a global lighting cut.
     * Moving emitters/occluders use short, locally clipped history instead.
     * Game lights are compared at actual shaded surfaces by the guide pass.
     * Map/material/sky data is static for a world; begin_world invalidates it. */
    lights->selection[2] = previous_count;
    lights->selection[3] = previous_count != lights->counts[1] ||
        memcmp(previous_positions, lights->positions, sizeof(previous_positions)) ||
        memcmp(previous_colors, lights->colors, sizeof(previous_colors));
    if (lights->selection[3] != 0) ++pt.local_light_updates;
    memcpy(lights->previous_positions, previous_positions, sizeof(previous_positions));
    memcpy(lights->previous_colors, previous_colors, sizeof(previous_colors));
    float current_medium[4];
    camera_medium(origin, current_medium);
    radiance_hash = hash_bytes(2166136261u, current_medium, sizeof(current_medium));
    radiance_hash = hash_bytes(radiance_hash, &ambient, sizeof(ambient));
    radiance_hash = hash_bytes(radiance_hash, &flash_brightness, sizeof(flash_brightness));
    radiance_hash = hash_bytes(radiance_hash, &flash_light_scale, sizeof(flash_light_scale));
    radiance_hash = hash_bytes(radiance_hash, &rocket_brightness, sizeof(rocket_brightness));
    radiance_hash = hash_bytes(radiance_hash, &rocket_light_scale, sizeof(rocket_light_scale));
    radiance_hash = hash_bytes(radiance_hash, &explosion_light_scale, sizeof(explosion_light_scale));
    radiance_hash = hash_bytes(radiance_hash, &lightning_light_scale, sizeof(lightning_light_scale));
    radiance_hash = hash_bytes(radiance_hash, &sun_angle, sizeof(sun_angle));
    radiance_hash = hash_bytes(radiance_hash, &sun_scale, sizeof(sun_scale));
    radiance_hash = hash_bytes(radiance_hash, &light_radius, sizeof(light_radius));
    pt_rr.frame_ready = !pt.software && rr_requested();
    if (pt_rr.frame_ready != pt_rr.previous_active) pt.temporal_valid = qfalse;
    uint32_t rr_sampling = pt_rr.frame_ready && (pt.lighting_mode == 57 || pt.lighting_mode == 61 ||
        pt.lighting_mode == 121 || pt.lighting_mode == 125) &&
        r_pathTracingCompactTransport->integer && !r_pathTracingStaged->integer ?
        (r_pathTracingLightReuse->integer ? 1u : 0u) | (r_pathTracingAdaptive->integer ? 2u : 0u) : 0;
    if (rr_sampling != pt_rr.sampling_flags) pt.temporal_valid = qfalse;
    pt_rr.sampling_flags = rr_sampling;
    pt_rr.previous_active = pt_rr.frame_ready;
    temporal_enabled = (c[27] != 0 && r_pathTracingTemporal->integer) || pt_rr.frame_ready;
    reset_reasons = (reset_history ? 1u : 0u) | (!pt.temporal_valid ? 2u : 0u) |
        (radiance_hash != pt.previous_radiance_hash ? 4u : 0u) |
        ((backEnd.refdef.rd.time < pt.previous_time ||
          backEnd.refdef.rd.time - pt.previous_time > 250) ? 8u : 0u);
    if (pt.temporal_valid) {
        const float *previous = pt.previous_temporal_camera;
        vec3_t movement;
        VectorSubtract(c, previous, movement);
        if (DotProduct(movement, movement) > 128.0f * 128.0f ||
            DotProduct(c + 4, previous + 4) < 0.5f ||
            fabsf(c[11] - previous[11]) > 0.0001f ||
            fabsf(c[15] - previous[15]) > 0.0001f ||
            c[21] != previous[21] ||
            c[28] != previous[28] || c[29] != previous[29] ||
            pt.previous_sampling != r_pathTracingSampling->integer)
            reset_reasons |= 16u;
    }
    reject_history = reset_reasons != 0;
    memset(reconstruction, 0, sizeof(*reconstruction));
    memcpy(reconstruction->origin, pt.previous_temporal_camera, sizeof(float) * 4);
    memcpy(reconstruction->forward, pt.previous_temporal_camera + 4, sizeof(float) * 4);
    memcpy(reconstruction->right, pt.previous_temporal_camera + 8, sizeof(float) * 4);
    memcpy(reconstruction->up, pt.previous_temporal_camera + 12, sizeof(float) * 4);
    reconstruction->jitter_history_reset[0] = pt.previous_temporal_camera[22];
    reconstruction->jitter_history_reset[1] = pt.previous_temporal_camera[23];
    reconstruction->jitter_history_reset[2] = geometry_changed && !(pt.software && sw_history->integer) ?
        MIN(r_pathTracingHistory->integer, 4) : r_pathTracingHistory->integer;
    reconstruction->jitter_history_reset[3] = reject_history ? 1 : 0;
    reconstruction->options[0] = temporal_enabled ? 1 : 0;
    reconstruction->options[1] = r_pathTracingTemporalDebug->integer;
    reconstruction->options[2] = r_pathTracingDebug->integer;
    reconstruction->options[3] = r_pathTracingSampling->integer;
    reconstruction->depth_projection[0] = projection[10];
    reconstruction->depth_projection[1] = projection[14];
    reconstruction->depth_projection[2] = pt.material_time;
    reconstruction->depth_projection[3] = pt.previous_material_time;
    pt.previous_material_time = pt.material_time;
    memcpy(reconstruction->camera_medium, current_medium, sizeof(current_medium));
    if (temporal_enabled && reject_history) {
        ++pt.temporal_resets;
        for (i = 0; i < 5; ++i)
            if (reset_reasons & (1u << i)) ++pt.temporal_reset_reasons[i];
    }
    pt.previous_lights_hash = light_hash;
    pt.previous_radiance_hash = radiance_hash;
    pt.previous_sampling = r_pathTracingSampling->integer;
    pt.previous_time = backEnd.refdef.rd.time;
    memcpy(pt.previous_temporal_camera, c, sizeof(c));
    /* The fixed-view reference and the live temporal estimator must not feed
     * accumulated samples into each other. Live input is always this frame. */
    if (geometry_changed || lights_changed || ambient_changed || penumbra_changed) { pt.history = 0; ++pt.scene_resets; }
    /* Exposure never affects camera reprojection, so ignore it for camera resets. */
    float camera_compare[32];
    memcpy(camera_compare, c, sizeof(camera_compare));
    camera_compare[19] = pt.previous_camera[19];
    if (memcmp(camera_compare, pt.previous_camera, sizeof(camera_compare))) { pt.history = 0; ++pt.camera_resets; }
    memcpy(pt.previous_camera, c, sizeof(c));
    pt.previous_hash = hash;
    c[30] = (float)(pt.frame++ % 1048576u);
    c[31] = r_pathTracingReference->integer ? (float)pt.history : 0;
    if (!r_pathTracingReference->integer) pt.history = 0;
    else if (pt.history < 4096) ++pt.history;
    if (pt.history == 32)
        ri.Printf(PRINT_ALL, "Path tracing reference accumulation: %u samples per pixel\n",
            pt.history * r_pathTracingSamples->integer);
    if (!pt.software) {
    as.accelerationStructureCount = 1;
    as.pAccelerationStructures = &scene;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &as; write.dstSet = pt.set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    }
    bind_buffer(4, positions, pt.max_vertices * sizeof(float) * 3);
    bind_buffer(5, indices, pt.max_indices * sizeof(uint32_t));
    for (i = 0; i < 2; ++i) {
        bind_buffer(12 + i, pt.guides[i].buffer, pt.guides[i].size);
        bind_buffer(19 + i, pt.previous_guides[i].buffer, pt.previous_guides[i].size);
    }
    bind_buffer(18, pt.history_color[pt.reconstruction_index ^ 1].buffer,
        pt.history_color[pt.reconstruction_index ^ 1].size);
    bind_buffer(21, pt.history_color[pt.reconstruction_index].buffer,
        pt.history_color[pt.reconstruction_index].size);
    bind_buffer(28, pt.spec_history[pt.reconstruction_index ^ 1].buffer,
        pt.spec_history[pt.reconstruction_index ^ 1].size);
    bind_buffer(29, pt.spec_history[pt.reconstruction_index].buffer,
        pt.spec_history[pt.reconstruction_index].size);
    bind_buffer(32, pt.shading_guide.buffer, pt.shading_guide.size);
    bind_buffer(33, pt.previous_shading_guide.buffer, pt.previous_shading_guide.size);
    bind_buffer(36, pt.reflection_guide.buffer, pt.reflection_guide.size);
    bind_buffer(37, pt.previous_reflection_guide.buffer, pt.previous_reflection_guide.size);
    bind_buffer(39, pt.moments[pt.reconstruction_index ^ 1].buffer, pt.moments[pt.reconstruction_index ^ 1].size);
    bind_buffer(40, pt.moments[pt.reconstruction_index].buffer, pt.moments[pt.reconstruction_index].size);
    bind_buffer(43, pt.transmission_history[pt.reconstruction_index ^ 1].buffer,
        pt.transmission_history[pt.reconstruction_index ^ 1].size);
    bind_buffer(44, pt.transmission_history[pt.reconstruction_index].buffer,
        pt.transmission_history[pt.reconstruction_index].size);
    bind_buffer(45, pt.transmission_guide.buffer, pt.transmission_guide.size);
    bind_buffer(46, pt.previous_transmission_guide.buffer, pt.previous_transmission_guide.size);
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    /* Static vertex attributes and triangle-to-material IDs are immutable
     * between map loads. Animated material records have their own dirty flag;
     * never cache the moving surfaces/marks appended after this prefix. */
    VkDeviceSize attribute_offset = pt.world_upload_pending ? 0 : pt.world_vertices*sizeof(pt_vertex_t);
    VkDeviceSize triangle_offset = pt.world_upload_pending ? 0 : pt.world_indices/3*sizeof(uint32_t);
    VkDeviceSize attribute_bytes = vertex_count*sizeof(pt_vertex_t)-attribute_offset;
    VkDeviceSize triangle_bytes = index_count/3*sizeof(uint32_t)-triangle_offset;
    buffer_upload_range(cmd, &pt.attributes, attribute_offset, attribute_bytes);
    buffer_upload_range(cmd, &pt.triangle_materials, triangle_offset, triangle_bytes);
    pt.geometry_upload_bytes = attribute_bytes+triangle_bytes;
    pt.world_upload_pending = qfalse;
    if (pt.materials_dirty) {
        buffer_upload(cmd, &pt.materials, pt.material_count*sizeof(pt_material_t));
        pt.materials_dirty = qfalse;
    }
    buffer_upload(cmd, &pt.lights, sizeof(pt_lights_t));
    if (pt.fog_dirty) {
        buffer_upload(cmd, &pt.fog, pt.fog.size);
        pt.fog_dirty = qfalse;
    }
    buffer_upload(cmd, &pt.temporal_params, sizeof(pt_temporal_params_t));
    if (pt.sampling_dirty) {
        buffer_upload(cmd, &pt.light_grid, 32+pt.light_cells*pt.map_light_count*16);
        buffer_upload(cmd, &pt.blue_noise, sizeof(pt_blue_noise));
        pt.sampling_dirty = qfalse;
    }
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.layout, 0, 1, &pt.set, 0, NULL);
    if (material_cache_active) {
        qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.material_cache_pipeline);
        c[28] = (float)pt.material_count;
        qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
        qvkCmdDispatch(cmd, (pt.material_count * 9u + 63u) / 64u, 1, 1);
        c[28] = (float)r_pathTracingSamples->integer;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    }
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.guide_pipeline);
    qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
    if (pt_rr.frame_ready) { c[27] = (float)pt_rr.sampling_flags; rr_prepare(cmd, c); }
    qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    uint32_t lighting_mode = pt.lighting_mode;
    qboolean brdf_active = (lighting_mode & 1) != 0;
    qboolean map_light_cull_active = (lighting_mode & 2) != 0;
    if (brdf_active != pt.brdf_active)
        ri.Printf(PRINT_ALL, "PT_BRDF_MODE enabled=%d requested=%d\n", brdf_active, r_pathTracingBRDFReuse->integer);
    pt.brdf_active = brdf_active;
    if (map_light_cull_active != pt.map_light_cull_active)
        ri.Printf(PRINT_ALL, "PT_MAP_LIGHT_MODE enabled=%d requested=%d\n", map_light_cull_active, r_pathTracingMapLightCull->integer);
    pt.map_light_cull_active = map_light_cull_active;
    qboolean alias_pdf_active = (lighting_mode & 4) != 0;
    if (alias_pdf_active != pt.alias_pdf_active)
        ri.Printf(PRINT_ALL, "PT_ALIAS_PDF_MODE enabled=%d requested=%d\n", alias_pdf_active, r_pathTracingAliasPDF->integer);
    pt.alias_pdf_active = alias_pdf_active;
    qboolean emitter_geometry_active = (lighting_mode & 8) != 0;
    if (emitter_geometry_active != pt.emitter_geometry_active)
        ri.Printf(PRINT_ALL, "PT_EMITTER_GEOMETRY_MODE enabled=%d emitters=%u\n",
            emitter_geometry_active, lights->counts[0]);
    pt.emitter_geometry_active = emitter_geometry_active;
    qboolean light_loop_active = (lighting_mode & 16) != 0;
    if (light_loop_active != pt.light_loop_active)
        ri.Printf(PRINT_ALL, "PT_LIGHT_LOOP enabled=%d requested=%d\n", light_loop_active, r_pathTracingLightLoop->integer);
    pt.light_loop_active = light_loop_active;
    pt.profile_instrumented = !pt.software && shader_profile_bind(cmd, brdf_active, map_light_cull_active, alias_pdf_active, emitter_geometry_active, light_loop_active);
    qboolean compact_active = !pt.software && !pt.profile_instrumented && compact_transport_select(pt.lighting_mode);
    if (compact_active != pt.compact_transport_active)
        ri.Printf(PRINT_ALL, "PT_COMPACT_TRANSPORT enabled=%d requested=%d\n", compact_active, r_pathTracingCompactTransport->integer);
    pt.compact_transport_active = compact_active;
    qboolean rr_trace = pt_rr.frame_ready && compact_active && !staged_active;
    if (rr_trace) {
        if (pt_rr.sampling_flags & 1u) {
            rr_bind(cmd, pt_rr.spatial, c);
            qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
            rr_barrier(cmd);
        }
        rr_bind(cmd, pt_rr.trace[(lighting_mode & 4) ? 1 : 0], c);
    } else if (!pt.profile_instrumented) {
        // RR guide's two-set layout is incompatible with the native one-set
        // layout. Rebind both descriptors and push constants explicitly.
        qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.layout, 0, 1, &pt.set, 0, NULL);
        qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
        qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        compact_active ? pt.compact_transport_pipeline[(lighting_mode & 4) ? 1 : 0] : pt.lighting_pipelines[lighting_mode]);
    }
    vk_pt_profile(cmd, 3);
    if (!staged_active || (r_pathTracingStaged->integer == 2 && c[31] == 0))
        qvkCmdDispatch(cmd, (pt.width + 7) / 8,
            rr_trace ? (pt.height + pt_rr.rows - 1) / pt_rr.rows :
            (compact_active ? (pt.height + pt.compact_transport_rows - 1) / pt.compact_transport_rows : (pt.height + 7) / 8), 1);
    if (staged_active) staged_record(cmd, c);
    vk_pt_profile(cmd, 4);
    if (pt.profile_instrumented) shader_profile_finish(cmd);
    if(pt.software) sw_profile_record(cmd,c);
    if (pt_rr.frame_ready) rr_pack(cmd, c, rr_trace);
    if (pt_exposure_enabled()) pt_exposure_dispatch(cmd, c);
    pt_rr.direct_profile_frame = rr_trace;
    if(pt.software) vk_pt_profile(cmd,8);
    if (c[27] != 0 && !pt_rr.frame_ready) {
        /* Spatial reconstruction never feeds back into the unbiased reference
         * accumulator. Every pass reads a distinct source buffer. */
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        if (temporal_enabled) {
            qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
            qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.temporal_pipeline);
            qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
        }
        if(pt.software) vk_pt_profile(cmd,9);
        qboolean software_filtered = pt.software && sw_denoise_record(cmd,c);
        if(pt.software) vk_pt_profile(cmd,10);
        if(pt.software) qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pt.layout,0,1,&pt.set,0,NULL);
        qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.denoise_pipeline);
        vk_pt_profile(cmd, 5);
        for (i = 0; !software_filtered && i < 3; ++i) {
            qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
            c[30] = (float)i;
            qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
            qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
        }
        if (temporal_enabled) {
            for (i = 0; i < 2; ++i) {
                pt_buffer_t swap = pt.guides[i];
                pt.guides[i] = pt.previous_guides[i];
                pt.previous_guides[i] = swap;
            }
            pt.reconstruction_index ^= 1;
            pt_buffer_t shading_swap = pt.shading_guide;
            pt.shading_guide = pt.previous_shading_guide;
            pt.previous_shading_guide = shading_swap;
            pt_buffer_t reflection_swap = pt.reflection_guide;
            pt.reflection_guide = pt.previous_reflection_guide;
            pt.previous_reflection_guide = reflection_swap;
            pt_buffer_t transmission_swap = pt.transmission_guide;
            pt.transmission_guide = pt.previous_transmission_guide;
            pt.previous_transmission_guide = transmission_swap;
        }
    }
    pt.temporal_valid = temporal_enabled;
    if (c[27] == 0 || pt_rr.frame_ready) {
        if(pt.software) { vk_pt_profile(cmd,9); vk_pt_profile(cmd,10); }
        vk_pt_profile(cmd, 5);
    }
    vk_pt_profile(cmd, 6);
    if (!r_pathTracingReference->integer) pt.motion_frame ^= 1;
    if (!pt.logged) {
        ri.Printf(PRINT_ALL, "Path tracing active: %u triangles, %u textures, %u emissive triangles, %u game lights\n",
            index_count / 3, pt.texture_count, lights->counts[0], lights->counts[1]);
        pt.logged = qtrue;
    }
    return qtrue;
}

void vk_pt_info_f(void)
{
    if (pt.active) {
        const pt_lights_t *lights = pt.lights.mapped;
        for (uint32_t i = 0; i < pt_portal_count; ++i) {
            const pt_portal_t *p = &lights->portals[i];
            uint32_t id = material_id(pt_portals[i].surface->shader);
            const pt_material_t *m = (const pt_material_t *)pt.materials.mapped+id;
            ri.Printf(PRINT_ALL, "PT_PORTAL material %u layers %.0f range %.1f\n", id, m->composition[0], m->optical[3]);
            for (int layer = 0; layer < (int)m->composition[0]; ++layer) {
                const pt_layer_t *l = &m->layers[layer];
                ri.Printf(PRINT_ALL, "PT_PORTAL layer %d %s blend %x rgb %.0f alpha %.0f program %.0f\n", layer,
                    pt.textures[(int)l->images[0][0]]->imgName, (unsigned)l->generators[3],
                    l->meta[0], l->meta[1], l->vectors[0][3]);
            }
            ri.Printf(PRINT_ALL, "PT_PORTAL %u clip %.3f %.3f %.3f %.3f\n", i,
                p->clip[0], p->clip[1], p->clip[2], p->clip[3]);
            for (int row = 0; row < 3; ++row)
                ri.Printf(PRINT_ALL, "PT_PORTAL row %d %.3f %.3f %.3f %.3f\n", row,
                    p->rows[row][0], p->rows[row][1], p->rows[row][2], p->rows[row][3]);
        }
    }
    ri.Printf(PRINT_ALL, "RR sampling: light reuse %u, adaptive %u, ceiling %d spp, workgroup 8x%u\n",
        pt_rr.sampling_flags & 1u, (pt_rr.sampling_flags >> 1) & 1u, r_pathTracingSamples->integer, pt_rr.rows);
    ri.Printf(PRINT_ALL, "Path tracing staged prototype: requested %d, active %d, rows %u, state %llu bytes\n",
        r_pathTracingStaged->integer, pt.staged.active, pt.staged.rows, (unsigned long long)pt.staged.state.size);
    ri.Printf(PRINT_ALL, "Path tracing compact workgroup: 8x%u\n", pt.compact_transport_rows);
    ri.Printf(PRINT_ALL, "Path tracing compact transport: requested %d, active %d\n",
        r_pathTracingCompactTransport->integer, pt.compact_transport_active);
    ri.Printf(PRINT_ALL, "Path tracing material cache: requested %d, active %d\n",
        r_pathTracingMaterialCache->integer, pt.material_cache_active);
    ri.Printf(PRINT_ALL, "Path tracing unified light loop: requested %d, active %d\n",
        r_pathTracingLightLoop->integer, pt.light_loop_active);
    ri.Printf(PRINT_ALL, "Path tracing packed emitter geometry: requested %d, active %d\n",
        r_pathTracingEmitterGeometry->integer, pt.emitter_geometry_active);
    ri.Printf(PRINT_ALL, "Path tracing BRDF reuse: requested %d, active %d, pipeline ready %d\n",
        r_pathTracingBRDFReuse->integer, pt.brdf_active,
        pt.lighting_pipelines[1] != VK_NULL_HANDLE || pt.lighting_pipelines[3] != VK_NULL_HANDLE ||
        pt.lighting_pipelines[5] != VK_NULL_HANDLE || pt.lighting_pipelines[7] != VK_NULL_HANDLE ||
        pt.compact_transport_pipeline[0] != VK_NULL_HANDLE ||
        pt.compact_transport_pipeline[1] != VK_NULL_HANDLE ||
        ((pt.lighting_mode & 1) && pt.lighting_pipelines[pt.lighting_mode] != VK_NULL_HANDLE));
    ri.Printf(PRINT_ALL, "Path tracing map-light rejection: requested %d, active %d\n",
        r_pathTracingMapLightCull->integer, pt.map_light_cull_active);
    ri.Printf(PRINT_ALL, "Path tracing cached alias PDF: requested %d, active %d\n",
        r_pathTracingAliasPDF->integer, pt.alias_pdf_active);
    vk_sl_info_f();
    ri.Printf(PRINT_ALL, "Path tracer: active %d, frame %u, accumulated frames %u, camera resets %u, scene resets %u\n",
        pt.active, pt.frame, pt.history, pt.camera_resets, pt.scene_resets);
    ri.Printf(PRINT_ALL, "Path tracing ambient fill: %.3f (0 disables; exposure %.3f)\n",
        r_pathTracingAmbient->value, r_pathTracingExposure->value);
    ri.Printf(PRINT_ALL, "Muzzle flash controls: brightness %.3f, light %.3f, tagged triangles %u\n",
        R_MuzzleFlashScale(r_muzzleFlashBrightness->value), R_MuzzleFlashScale(r_muzzleFlashLightScale->value),
        pt.muzzle_flash_triangles);
    ri.Printf(PRINT_ALL, "Rocket controls: brightness %.3f, light %.3f, tagged triangles %u\n",
        R_MuzzleFlashScale(r_rocketBrightness->value), R_MuzzleFlashScale(r_rocketLightScale->value),
        pt.rocket_triangles);
    ri.Printf(PRINT_ALL, "Weapon effect lights: rocket explosion %.3f (%u triangles), lightning gun %.3f (%u triangles)\n",
        R_MuzzleFlashScale(r_rocketExplosionLightScale->value), pt.rocket_explosion_triangles,
        R_MuzzleFlashScale(r_lightningGunLightScale->value), pt.lightning_gun_triangles);
    ri.Printf(PRINT_ALL, "Path tracing penumbra: sun angle %.3f degrees, sun scale %.3f, light radius %.3f units (0 = point source)\n",
        r_pathTracingSunAngle->value, r_pathTracingSunScale->value, r_pathTracingLightRadius->value);
    ri.Printf(PRINT_ALL, "Path tracing auto exposure: %s, exposure %.3f, target %.3f, speed %.2fs, range %.2f-%.2f\n",
        pt_exposure_enabled() ? "on" : "off", pt.active ? pt.exposure : r_pathTracingExposure->value,
        r_pathTracingAdaptiveTarget->value, r_pathTracingAdaptiveSpeed->value,
        r_pathTracingAdaptiveMin->value, r_pathTracingAdaptiveMax->value);
    ri.Printf(PRINT_ALL, "Path tracing temporal: valid %d, resets %u, history limit %d (moving geometry limit 4)\n",
        pt.temporal_valid, pt.temporal_resets, r_pathTracingHistory->integer);
    ri.Printf(PRINT_ALL, "Temporal reset causes: renderer %u, invalid %u, lights %u, time %u, camera/settings %u\n",
        pt.temporal_reset_reasons[0], pt.temporal_reset_reasons[1],
        pt.temporal_reset_reasons[2], pt.temporal_reset_reasons[3], pt.temporal_reset_reasons[4]);
    ri.Printf(PRINT_ALL, "Path tracing object motion: %u matched surfaces, %u new/untracked surfaces\n",
        pt.motion_matched, pt.motion_rejected);
    ri.Printf(PRINT_ALL, "Path tracing reconstruction: %u local light updates, variance moments, planar mirror motion; sampling %s, %u light cells\n",
        pt.local_light_updates, r_pathTracingSampling->integer ? "blue noise" : "white noise", pt.light_cells);
    ri.Printf(PRINT_ALL, "Path tracing materials: %u animated, %u transparent/additive, %u textures\n",
        pt.animated_materials, pt.effect_materials, pt.texture_count);
    ri.Printf(PRINT_ALL, "Path tracing transmission: up to 4 tracked optical events including internal reflection, history cap 4\n");
    ri.Printf(PRINT_ALL, "Path tracing effects: submitted polygons, %.0f attached decal triangles (16 layers per hit)\n",
        pt.active ? ((pt_lights_t *)pt.lights.mapped)->decal_bounds[0][3] : 0.0f);
    ri.Printf(PRINT_ALL, "Path tracing geometry uploads: %.0f bytes (last recorded frame; static map prefixes cached)\n",
        (double)(vk_rt_scene_upload_bytes()+pt.geometry_upload_bytes));
    ri.Printf(PRINT_ALL, "Path tracing water reflection: previous-wave/target motion, history cap 4, shared 32-byte motion record\n");
    uint32_t dielectric = 0, mapped = 0, texture_programs = 0;
    for (int i = 0; i < MAX_SHADERS; ++i) if (pt.material_shaders[i]) {
        const pt_material_t *m = (const pt_material_t *)pt.materials.mapped+i;
        if (m->params[0] == 4) ++dielectric;
        if (m->maps[0] >= 0 || m->maps[1] >= 0 || m->maps[2] >= 0) ++mapped;
        for (int layer = 0; layer < (int)m->composition[0]; ++layer)
            if (m->layers[layer].vectors[0][3] == 2 || m->layers[layer].vectors[0][3] == -2) ++texture_programs;
    }
    ri.Printf(PRINT_ALL, "Path tracing texture programs: %u UV-only stages, fast path %d\n",
        texture_programs, r_pathTracingMaterialFastPath->integer);
    ri.Printf(PRINT_ALL, "Path tracing transport: %u dielectrics, %u PBR mapped, %u BSP water volumes, camera IOR %.3f; separate diffuse/reflection histories\n",
        dielectric, mapped, pt.medium_count, pt.active ? ((pt_temporal_params_t *)pt.temporal_params.mapped)->camera_medium[0] : 1);
}

qboolean vk_pt_history_reset(void)
{
    return pt.active && ((pt_temporal_params_t *)pt.temporal_params.mapped)->jitter_history_reset[3] != 0;
}
