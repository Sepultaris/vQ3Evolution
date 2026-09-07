#include "vk_pathtrace.h"
#include "vk_instance.h"
#include "vk_shaders.h"
#include "vk_image.h"
#include "tr_globals.h"
#include "tr_backend.h"
#include "tr_shader.h"
#include "tr_light.h"
#include "tr_cvar.h"
#include "R_Parser.h"

#define PT_MAX_EMITTERS 8192
#define PT_MAX_LIGHTS 32
#define PT_MAX_MAP_LIGHTS 1024
#define PT_POINT_CAPACITY (PT_MAX_LIGHTS + PT_MAX_MAP_LIGHTS)
#define PT_MAX_MOTION_SURFACES 4096
#define PT_MOTION_BUCKETS 2048

/* std430 layouts shared with pathtrace.comp. No baked vertex/lightmap color. */
typedef struct { float normal[4], uv[4], previous[4], meta[4], color[4], local[4]; } pt_vertex_t;
typedef struct {
    uint32_t key[7], topology, first, count, ordinal;
    int next;
} pt_motion_surface_t;
typedef struct { float a[4], b[4], wave[4]; } pt_texmod_t;
typedef struct {
    float images[2][4], params[4], color[4], generators[4];
    float rgb_wave[4], alpha_wave[4], meta[4], vectors[2][4];
    pt_texmod_t mods[TR_MAX_TEXMODS];
} pt_layer_t;
typedef struct {
    float surface[4], emission[4], tint[4], params[4];
    float maps[4], optical[4], absorption[4];
    pt_layer_t layers[1 + MAX_SHADER_STAGES];
} pt_material_t;
typedef struct { uint32_t primitive; float cumulative_power; } pt_emitter_t;
typedef struct { uint32_t first, count; float ior; vec3_t absorption; } pt_medium_t;
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
} pt_lights_t;
typedef char pt_vertex_layout_check[(sizeof(pt_vertex_t) == 96) ? 1 : -1];
typedef char pt_layer_layout_check[(sizeof(pt_layer_t) == 352) ? 1 : -1];
typedef char pt_material_layout_check[(sizeof(pt_material_t) == 3280) ? 1 : -1];
typedef char pt_lights_layout_check[(sizeof(pt_lights_t) == 32 + PT_POINT_CAPACITY * 48 + 8192 * 8) ? 1 : -1];
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
    vec3_t sun_color, sun_direction;
    pt_vertex_t *world_attributes;
    uint32_t *world_materials;
    shader_t *material_shaders[MAX_SHADERS];
    image_t *textures[PT_MAX_TEXTURES];
    pt_buffer_t attributes, triangle_materials, materials, lights, accumulation;
    pt_buffer_t guides[3], filter[2];
    pt_buffer_t previous_guides[2], history_color[2], temporal_params;
    pt_buffer_t motion_guides[2];
    pt_buffer_t specular, visible_emission, spec_history[2], spec_filter[2];
    pt_buffer_t shading_guide, previous_shading_guide;
    vec3_t *motion_positions[2];
    pt_motion_surface_t motion_surfaces[2][PT_MAX_MOTION_SURFACES];
    int motion_buckets[2][PT_MOTION_BUCKETS];
    uint32_t motion_frame, motion_count[2], motion_vertices;
    uint32_t motion_matched, motion_rejected;
    float material_time;
    uint32_t animated_materials, effect_materials;
    uint32_t material_count;
    qboolean materials_dirty;
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
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkPipeline denoise_pipeline;
    VkPipeline temporal_pipeline;
    VkSampler sampler, clamp_sampler;
} pt;
static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size);

void vk_pt_profile(VkCommandBuffer cmd, uint32_t point)
{
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
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 7 };
        if (!create || !pt.destroy_queries || !pt.get_queries || !pt.reset_queries || !pt.write_timestamp ||
            create(vk.device, &info, NULL, &pt.profile_pool) != VK_SUCCESS) return;
    }
    if (point == 0) {
        int now = ri.Milliseconds();
        uint64_t ticks[7];
        /* The existing render fence has completed. Never introduce a profiling
         * wait: unavailable/incomplete query sets are simply discarded. */
        if (pt.profile_mask == 127 && pt.get_queries(vk.device, pt.profile_pool, 0, 7,
            sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            double ms[6];
            for (int i = 0; i < 6; ++i) ms[i] = ((ticks[i+1]-ticks[i]) & pt.timestamp_mask)*pt.timestamp_period/1000000.0;
            ri.Printf(PRINT_ALL, "PT_PROFILE frame=%d raster=%.3f as=%.3f trace=%.3f temporal=%.3f spatial=%.3f post=%.3f\n",
                now-pt.profile_time, ms[0], ms[1], ms[2], ms[3], ms[4], ms[5]);
        }
        pt.profile_time = now;
        pt.profile_mask = 0;
        pt.reset_queries(cmd, pt.profile_pool, 0, 7);
    }
    if (point > 6 || (point && !(pt.profile_mask & 1))) return;
    pt.write_timestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pt.profile_pool, point);
    pt.profile_mask |= 1u << point;
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

static void buffer_upload(VkCommandBuffer cmd, pt_buffer_t *b, VkDeviceSize size)
{
    VkBufferCopy copy = {0, 0, size};
    if (!size) return;
    memcpy(b->upload_mapped, b->mapped, (size_t)size);
    qvkCmdCopyBuffer(cmd, b->upload, b->buffer, 1, &copy);
}

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
    out->generators[3] = (stage->stateBits & GLS_SRCBLEND_BITS) == GLS_SRCBLEND_SRC_ALPHA ? 1 : 0;
    for (i = 0; i < 4; ++i) out->color[i] = stage->constantColor[i] / 255.0f;
    copy_wave(out->rgb_wave, &stage->rgbWave);
    copy_wave(out->alpha_wave, &stage->alphaWave);
    memcpy(out->vectors[0], bundle->tcGenVectors[0], sizeof(vec3_t));
    memcpy(out->vectors[1], bundle->tcGenVectors[1], sizeof(vec3_t));
    animated = bundle->numImageAnimations > 1 || stage->rgbGen == CGEN_WAVEFORM || stage->alphaGen == AGEN_WAVEFORM;
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
    }
    return animated;
}

static uint32_t material_id(shader_t *shader)
{
    uint32_t id;
    int s, b;
    shaderStage_t *stage = NULL;
    textureBundle_t *base_bundle = NULL, *emissive_bundle = NULL;
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
    for (s = 0; s < shader->numUnfoggedPasses; ++s) {
        shaderStage_t *candidate = shader->stages[s];
        if (!candidate) continue;
        for (b = 0; b < NUM_TEXTURE_BUNDLES; ++b) {
            if (!candidate->bundle[b].isLightmap && candidate->bundle[b].image[0]) {
                textureBundle_t *bundle = &candidate->bundle[b];
                qboolean additive = ((candidate->stateBits & GLS_DSTBLEND_BITS) == GLS_DSTBLEND_ONE &&
                    (candidate->stateBits & GLS_SRCBLEND_BITS) != GLS_SRCBLEND_ZERO) ||
                    (b == 1 && shader->multitextureEnv == GL_ADD);
                // Legacy environment-map stages are raster reflection tricks,
                // not light emitters. Specular response belongs to the BRDF.
                if (bundle->tcGen == TCGEN_ENVIRONMENT_MAPPED) continue;
                if (additive) {
                    emissive_bundle = bundle;
                    int layer = (int)m->params[2] + 1;
                    if (layer <= MAX_SHADER_STAGES) {
                        if (layer_initialize(&m->layers[layer], candidate, bundle, shader)) m->params[3] = 1;
                        m->params[2] = layer;
                    }
                }
                if (!additive && !base_bundle) { base_bundle = bundle; stage = candidate; base = bundle->image[0]; }
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
    m->tint[0] = m->tint[1] = m->tint[2] = m->tint[3] = 1;
    if (stage && stage->rgbGen == CGEN_CONST)
        for (s = 0; s < 3; ++s) m->tint[s] = stage->constantColor[s] / 255.0f;
    m->params[1] = base_bundle != NULL;
    if (layer_initialize(&m->layers[0], stage, base_bundle, shader)) m->params[3] = 1;
    if (emissive_bundle && m->emission[1] <= 0) m->emission[1] = 1;
    if (!base_bundle && emissive_bundle) m->params[0] = 2; // additive, no opaque backing
    else if (stage && shader->sort > SS_OPAQUE) {
        unsigned blend = stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
        if (blend == (GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA)) m->params[0] = 1;
        else if (blend == (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO) ||
                 blend == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR)) m->params[0] = 3;
    }
    if (m->params[3] != 0) ++pt.animated_materials;
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
        if (strstr(shader->name, "metal") || strstr(shader->name, "chrome")) {
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
    if (m->params[0] != 0) ++pt.effect_materials;
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
    memset(pt.material_shaders, 0, sizeof(pt.material_shaders));
    pt.texture_count = pt.history = 0;
    pt.material_count = 0;
    pt.animated_materials = pt.effect_materials = 0;
    pt.frozen = qfalse;
    pt.logged = qfalse;
    pt.temporal_valid = qfalse;
    pt.motion_count[0] = pt.motion_count[1] = 0;
    memset(pt.motion_buckets, -1, sizeof(pt.motion_buckets));
    load_map_lights();
    VectorClear(pt.sun_color);
    VectorSet(pt.sun_direction, 0, 0, 1);
    for (surface = 0; surface < tr.world->bmodels[0].numSurfaces; ++surface) {
        const shader_t *shader = tr.world->bmodels[0].firstSurface[surface].shader;
        if (!shader->rtSunDefined || !(shader->isSky || (shader->surfaceFlags & SURF_SKY))) continue;
        VectorCopy(shader->rtSunColor, pt.sun_color);
        VectorCopy(shader->rtSunDirection, pt.sun_direction);
        break;
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

void vk_pt_world_vertex(uint32_t vertex, const float *normal, const float *uv)
{
    if (!pt.active) return;
    memcpy(pt.world_attributes[vertex].normal, normal, 3 * sizeof(float));
    memcpy(pt.world_attributes[vertex].uv, uv, 2 * sizeof(float));
    for (int i = 0; i < 4; ++i) pt.world_attributes[vertex].color[i] = 1;
}

void vk_pt_world_surface(uint32_t first_index, uint32_t index_count, shader_t *shader)
{
    uint32_t i, id;
    if (!pt.active) return;
    id = material_id(shader);
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

uint32_t vk_pt_partition_dynamic(uint32_t *indices, uint32_t count)
{
    uint32_t cursor = 0, regular = 0;
    uint32_t *materials = pt.triangle_materials.mapped;
    // Stable partition changes triangle order only, never vertex correspondence.
    for (int weapon = 0; weapon <= 1; ++weapon) {
        for (uint32_t i = pt.world_indices/3; i < count/3; ++i) {
            if (((materials[i]&0x20000000u) != 0) != weapon) continue;
            memcpy(pt.partition_indices+cursor, indices+i*3, 3*sizeof(uint32_t));
            pt.partition_materials[cursor/3] = materials[i];
            cursor += 3;
        }
        if (!weapon) regular = cursor;
    }
    memcpy(indices+pt.world_indices, pt.partition_indices, cursor*sizeof(uint32_t));
    memcpy(materials+pt.world_indices/3, pt.partition_materials, cursor/3*sizeof(uint32_t));
    return pt.world_indices+regular;
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
        { "vqe/test/flat", "{\nrt_material 0.5 0\nrt_basecolormap *pt_base\nrt_normalmap *pt_normal\nrt_normalscale 0\nrt_ormmap *pt_orm\n{ map $whiteimage }\n}" }
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
        vk_pt_world_vertex(*vc+i, normal, uv);
    }
    for (int i = 0; i < 6; ++i) indices[*ic+i] = *vc+triangles[i];
    vk_pt_world_surface(*ic, 6, R_FindShader(material, LIGHTMAP_NONE, qtrue));
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

void vk_pt_begin_frame(void)
{
    if (!pt.active) return;
    pt.motion_count[pt.motion_frame] = pt.motion_vertices = 0;
    pt.motion_matched = pt.motion_rejected = 0;
    memset(pt.motion_buckets[pt.motion_frame], -1, sizeof(pt.motion_buckets[pt.motion_frame]));
    if (pt.world_vertices) memcpy(pt.attributes.mapped, pt.world_attributes,
        pt.world_vertices * sizeof(pt_vertex_t));
    if (pt.world_indices) memcpy(pt.triangle_materials.mapped, pt.world_materials,
        (pt.world_indices / 3) * sizeof(uint32_t));
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
    id = material_id(shader);
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
            if (!memcmp(candidate->key, key, sizeof(key)) && candidate->ordinal == ordinal &&
                candidate->topology == topology && candidate->count == vertex_count) {
                previous = candidate;
                break;
            }
        }
        if (previous) {
            for (i = 0; i < vertex_count; ++i) {
                vec3_t delta;
                VectorSubtract(world_positions + i * 3, pt.motion_positions[old][previous->first + i], delta);
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
        current->next = pt.motion_buckets[frame][bucket];
        pt.motion_buckets[frame][bucket] = entry;
        memcpy(pt.motion_positions[frame] + pt.motion_vertices, world_positions, vertex_count * sizeof(vec3_t));
        pt.motion_vertices += vertex_count;
    }
    if (previous) ++pt.motion_matched; else ++pt.motion_rejected;
    vertices = (pt_vertex_t *)pt.attributes.mapped + first_vertex;
	/* Low 16 bits are the material; high bits are per-object ray visibility. */
    if (backEnd.currentEntity->e.renderfx & RF_THIRD_PERSON) id |= 0x80000000u;
    if (backEnd.currentEntity->e.renderfx & RF_NOSHADOW) id |= 0x40000000u;
    if (backEnd.currentEntity->e.renderfx & (RF_FIRST_PERSON | RF_DEPTHHACK)) id |= 0x20000000u;
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
        if (previous) {
            memcpy(vertices[i].previous, pt.motion_positions[pt.motion_frame ^ 1][previous->first + i], sizeof(vec3_t));
            vertices[i].previous[3] = 1;
        }
    }
    for (i = 0; i < index_count / 3; ++i) triangles[i] = id;
}

qboolean vk_pt_initialize(uint32_t width, uint32_t height,
    uint32_t max_vertices, uint32_t max_indices,
    VkImageView color, VkImageView depth, VkImageView output,
    VkImageView motion, VkImageView path_depth)
{
    uint32_t i;
    VkDescriptorSetLayoutBinding bindings[34] = {0};
    VkPhysicalDeviceProperties properties;
    VkDescriptorSetLayoutCreateInfo set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkDescriptorPoolSize sizes[] = {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PT_MAX_TEXTURES + 2 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 27 }
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
    extern unsigned char pathtrace_comp_spv[];
    extern int pathtrace_comp_spv_size;
    extern unsigned char pt_denoise_comp_spv[];
    extern int pt_denoise_comp_spv_size;
    extern unsigned char pt_temporal_comp_spv[];
    extern int pt_temporal_comp_spv_size;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    if ((VkDeviceSize)width * height * sizeof(float) * 4 > properties.limits.maxStorageBufferRange ||
        max_vertices * sizeof(pt_vertex_t) > properties.limits.maxStorageBufferRange ||
        MAX_SHADERS * sizeof(pt_material_t) > properties.limits.maxStorageBufferRange) {
        ri.Printf(PRINT_WARNING, "Path tracing: resolution/scene exceeds device storage-buffer range\n");
        return qfalse;
    }
    if (!buffer_create(&pt.attributes, max_vertices * sizeof(pt_vertex_t), qtrue) ||
        !buffer_create(&pt.triangle_materials, (max_indices / 3) * sizeof(uint32_t), qtrue) ||
        !buffer_create(&pt.materials, MAX_SHADERS * sizeof(pt_material_t), qtrue) ||
        !buffer_create(&pt.lights, sizeof(pt_lights_t), qtrue) ||
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
    if (!buffer_create(&pt.specular, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.visible_emission, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.shading_guide, (VkDeviceSize)width * height * 16, qfalse) ||
        !buffer_create(&pt.previous_shading_guide, (VkDeviceSize)width * height * 16, qfalse)) goto fail;
    for (i = 0; i < 2; ++i)
        if (!buffer_create(&pt.spec_history[i], (VkDeviceSize)width * height * 16, qfalse) ||
            !buffer_create(&pt.spec_filter[i], (VkDeviceSize)width * height * 16, qfalse)) goto fail;
    pt.motion_positions[1] = malloc(max_vertices * sizeof(vec3_t));
    if (!pt.motion_positions[0] || !pt.motion_positions[1]) goto fail;
    memset(pt.motion_buckets, -1, sizeof(pt.motion_buckets));
    for (i = 0; i < ARRAY_LEN(bindings); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorCount = i == 9 ? PT_MAX_TEXTURES : 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].descriptorType = bindings[2].descriptorType =
        bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[24].descriptorType = bindings[25].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    set_info.bindingCount = ARRAY_LEN(bindings);
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
    module_info.codeSize = pathtrace_comp_spv_size;
    module_info.pCode = (const uint32_t *)pathtrace_comp_spv;
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = module;
    pipeline.stage.pName = "main";
    pipeline.layout = pt.layout;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    module_info.codeSize = pt_denoise_comp_spv_size;
    module_info.pCode = (const uint32_t *)pt_denoise_comp_spv;
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.module = module;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.denoise_pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    module_info.codeSize = pt_temporal_comp_spv_size;
    module_info.pCode = (const uint32_t *)pt_temporal_comp_spv;
    VK_CHECK(qvkCreateShaderModule(vk.device, &module_info, NULL, &module));
    pipeline.stage.module = module;
    VK_CHECK(qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.temporal_pipeline));
    qvkDestroyShaderModule(vk.device, module, NULL);
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(qvkCreateSampler(vk.device, &sampler, NULL, &pt.sampler));
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(qvkCreateSampler(vk.device, &sampler, NULL, &pt.clamp_sampler));
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
    pt.width = width;
    pt.height = height;
    pt.max_vertices = max_vertices;
    pt.max_indices = max_indices;
    pt.active = qtrue;
    ri.Printf(PRINT_ALL, "Experimental native path tracer initialized at %ux%u\n", width, height);
    return qtrue;
fail:
    vk_pt_shutdown();
    return qfalse;
}

void vk_pt_shutdown(void)
{
    free(pt.partition_indices); free(pt.partition_materials);
    if (pt.profile_pool) pt.destroy_queries(vk.device, pt.profile_pool, NULL);
    free(pt.media); free(pt.medium_planes);
    free(pt.world_attributes);
    free(pt.world_materials);
    buffer_destroy(&pt.attributes);
    buffer_destroy(&pt.triangle_materials);
    buffer_destroy(&pt.materials);
    buffer_destroy(&pt.lights);
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
        buffer_destroy(&pt.spec_history[i]);
        buffer_destroy(&pt.spec_filter[i]);
    }
    free(pt.motion_positions[0]);
    free(pt.motion_positions[1]);
    if (pt.pipeline) qvkDestroyPipeline(vk.device, pt.pipeline, NULL);
    if (pt.denoise_pipeline) qvkDestroyPipeline(vk.device, pt.denoise_pipeline, NULL);
    if (pt.temporal_pipeline) qvkDestroyPipeline(vk.device, pt.temporal_pipeline, NULL);
    if (pt.layout) qvkDestroyPipelineLayout(vk.device, pt.layout, NULL);
    if (pt.pool) qvkDestroyDescriptorPool(vk.device, pt.pool, NULL);
    if (pt.set_layout) qvkDestroyDescriptorSetLayout(vk.device, pt.set_layout, NULL);
    if (pt.sampler) qvkDestroySampler(vk.device, pt.sampler, NULL);
    if (pt.clamp_sampler) qvkDestroySampler(vk.device, pt.clamp_sampler, NULL);
    memset(&pt, 0, sizeof(pt));
}

static uint32_t hash_bytes(uint32_t hash, const void *data, size_t size)
{
    const unsigned char *bytes = data;
    size_t i;
    for (i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
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
    const uint32_t *triangle_materials = pt.triangle_materials.mapped;
    const pt_material_t *materials = pt.materials.mapped;
    VkWriteDescriptorSetAccelerationStructureKHR as = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    if (!pt.active || !pt.world_vertices) return qfalse;
    if (!r_pathTracingReference->integer || !pt.frozen) pt.material_time = backEnd.refdef.rd.time * 0.001f;
    (void)sun;
    if (pt.textures_dirty) {
        VkDescriptorImageInfo textures[PT_MAX_TEXTURES];
        for (i = 0; i < PT_MAX_TEXTURES; ++i) {
            image_t *texture = i < pt.texture_count ? pt.textures[i] : tr.whiteImage;
            textures[i] = (VkDescriptorImageInfo){ texture->wrapClampMode == GL_REPEAT ? pt.sampler : pt.clamp_sampler,
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
    if (!r_pathTracingReference->integer || !pt.frozen) {
        memset(lights, 0, sizeof(*lights));
        lights->selection[1] = pt.world_opaque_indices/3;
        for (i = 0; i < index_count / 3; ++i) {
            vec3_t e1, e2, cross;
            float power = materials[triangle_materials[i] & 0xffffu].emission[1];
            const float *a, *b, *c;
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
            lights->emitters[lights->counts[0]].primitive = i;
            lights->emitters[lights->counts[0]++].cumulative_power = lights->selection[0];
        }
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
    pt.frozen = r_pathTracingReference->integer != 0;
    c[0] = origin[0]; c[1] = origin[1]; c[2] = origin[2]; c[3] = near_distance;
    for (i = 0; i < 3; ++i) {
        c[4+i] = axis[i]; c[8+i] = -axis[3+i]; c[12+i] = axis[6+i];
        c[16+i] = pt.sun_direction[i]; c[24+i] = pt.sun_color[i] / 100.0f;
    }
    c[7] = far_distance; c[11] = projection[0]; c[15] = projection[5];
    c[19] = r_pathTracingExposure->value;
    c[20] = 0.02f; c[21] = ray_distance;
    c[22] = projection[8]; c[23] = projection[9];
    c[27] = r_pathTracingDenoise->integer && !r_pathTracingReference->integer ? 1.0f : 0.0f;
    c[28] = r_pathTracingSamples->integer; c[29] = r_pathTracingBounces->integer;
    hash = hash_bytes(2166136261u, cpu_positions + pt.world_vertices * 3,
        (vertex_count - pt.world_vertices) * 3 * sizeof(float));
    hash = hash_bytes(hash, cpu_indices + pt.world_indices,
        (index_count - pt.world_indices) * sizeof(uint32_t));
    hash = hash_bytes(hash, triangle_materials + pt.world_indices / 3,
        ((index_count - pt.world_indices) / 3) * sizeof(uint32_t));
    geometry_changed = hash != pt.previous_hash;
    light_hash = hash_bytes(2166136261u, lights, sizeof(*lights));
    lights_changed = light_hash != pt.previous_lights_hash;
    /* The emitter CDF contains animated triangle indices/areas and visibility
     * counts. Changes there are geometry motion, not a global lighting cut.
     * Moving emitters/occluders use short, locally clipped history instead.
     * Game point-light changes (including muzzle flashes) invalidate globally.
     * Map/material/sky data is static for a world; begin_world invalidates it. */
    radiance_hash = hash_bytes(2166136261u, lights->counts + 1, sizeof(uint32_t));
    radiance_hash = hash_bytes(radiance_hash, lights->positions, sizeof(lights->positions));
    radiance_hash = hash_bytes(radiance_hash, lights->colors, sizeof(lights->colors));
    radiance_hash = hash_bytes(radiance_hash, lights->cones, sizeof(lights->cones));
    float current_medium[4];
    camera_medium(origin, current_medium);
    radiance_hash = hash_bytes(radiance_hash, current_medium, sizeof(current_medium));
    temporal_enabled = c[27] != 0 && r_pathTracingTemporal->integer;
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
            c[19] != previous[19] || c[21] != previous[21] ||
            c[28] != previous[28] || c[29] != previous[29])
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
    reconstruction->jitter_history_reset[2] = geometry_changed ?
        MIN(r_pathTracingHistory->integer, 4) : r_pathTracingHistory->integer;
    reconstruction->jitter_history_reset[3] = reject_history ? 1 : 0;
    reconstruction->options[0] = temporal_enabled ? 1 : 0;
    reconstruction->options[1] = r_pathTracingTemporalDebug->integer;
    reconstruction->options[2] = r_pathTracingDebug->integer;
    reconstruction->depth_projection[0] = projection[10];
    reconstruction->depth_projection[1] = projection[14];
    reconstruction->depth_projection[2] = pt.material_time;
    memcpy(reconstruction->camera_medium, current_medium, sizeof(current_medium));
    if (temporal_enabled && reject_history) {
        ++pt.temporal_resets;
        for (i = 0; i < 5; ++i)
            if (reset_reasons & (1u << i)) ++pt.temporal_reset_reasons[i];
    }
    pt.previous_lights_hash = light_hash;
    pt.previous_radiance_hash = radiance_hash;
    pt.previous_time = backEnd.refdef.rd.time;
    memcpy(pt.previous_temporal_camera, c, sizeof(c));
    /* The fixed-view reference and the live temporal estimator must not feed
     * accumulated samples into each other. Live input is always this frame. */
    if (geometry_changed || lights_changed) { pt.history = 0; ++pt.scene_resets; }
    if (memcmp(c, pt.previous_camera, sizeof(c))) { pt.history = 0; ++pt.camera_resets; }
    memcpy(pt.previous_camera, c, sizeof(c));
    pt.previous_hash = hash;
    c[30] = (float)(pt.frame++ % 1048576u);
    c[31] = r_pathTracingReference->integer ? (float)pt.history : 0;
    if (!r_pathTracingReference->integer) pt.history = 0;
    else if (pt.history < 4096) ++pt.history;
    if (pt.history == 32)
        ri.Printf(PRINT_ALL, "Path tracing reference accumulation: %u samples per pixel\n",
            pt.history * r_pathTracingSamples->integer);
    as.accelerationStructureCount = 1;
    as.pAccelerationStructures = &scene;
    memset(&write, 0, sizeof(write));
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext = &as; write.dstSet = pt.set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
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
    barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    buffer_upload(cmd, &pt.attributes, vertex_count*sizeof(pt_vertex_t));
    buffer_upload(cmd, &pt.triangle_materials, index_count/3*sizeof(uint32_t));
    if (pt.materials_dirty) {
        buffer_upload(cmd, &pt.materials, pt.material_count*sizeof(pt_material_t));
        pt.materials_dirty = qfalse;
    }
    buffer_upload(cmd, &pt.lights, sizeof(pt_lights_t));
    buffer_upload(cmd, &pt.temporal_params, sizeof(pt_temporal_params_t));
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.pipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.layout, 0, 1, &pt.set, 0, NULL);
    qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
    qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
    vk_pt_profile(cmd, 3);
    if (c[27] != 0) {
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
        qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.denoise_pipeline);
        vk_pt_profile(cmd, 4);
        for (i = 0; i < 3; ++i) {
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
        }
    }
    pt.temporal_valid = temporal_enabled;
    if (c[27] == 0) vk_pt_profile(cmd, 4);
    vk_pt_profile(cmd, 5);
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
    ri.Printf(PRINT_ALL, "Path tracer: active %d, frame %u, accumulated frames %u, camera resets %u, scene resets %u\n",
        pt.active, pt.frame, pt.history, pt.camera_resets, pt.scene_resets);
    ri.Printf(PRINT_ALL, "Path tracing temporal: valid %d, resets %u, history limit %d (moving geometry limit 4)\n",
        pt.temporal_valid, pt.temporal_resets, r_pathTracingHistory->integer);
    ri.Printf(PRINT_ALL, "Temporal reset causes: renderer %u, invalid %u, lights %u, time %u, camera/settings %u\n",
        pt.temporal_reset_reasons[0], pt.temporal_reset_reasons[1],
        pt.temporal_reset_reasons[2], pt.temporal_reset_reasons[3], pt.temporal_reset_reasons[4]);
    ri.Printf(PRINT_ALL, "Path tracing object motion: %u matched surfaces, %u new/untracked surfaces\n",
        pt.motion_matched, pt.motion_rejected);
    ri.Printf(PRINT_ALL, "Path tracing materials: %u animated, %u transparent/additive, %u textures\n",
        pt.animated_materials, pt.effect_materials, pt.texture_count);
    uint32_t dielectric = 0, mapped = 0;
    for (int i = 0; i < MAX_SHADERS; ++i) if (pt.material_shaders[i]) {
        const pt_material_t *m = (const pt_material_t *)pt.materials.mapped+i;
        if (m->params[0] == 4) ++dielectric;
        if (m->maps[0] >= 0 || m->maps[1] >= 0 || m->maps[2] >= 0) ++mapped;
    }
    ri.Printf(PRINT_ALL, "Path tracing transport: %u dielectrics, %u PBR mapped, %u BSP water volumes, camera IOR %.3f; separate diffuse/reflection histories\n",
        dielectric, mapped, pt.medium_count, pt.active ? ((pt_temporal_params_t *)pt.temporal_params.mapped)->camera_medium[0] : 1);
}

qboolean vk_pt_history_reset(void)
{
    return pt.active && ((pt_temporal_params_t *)pt.temporal_params.mapped)->jitter_history_reset[3] != 0;
}
