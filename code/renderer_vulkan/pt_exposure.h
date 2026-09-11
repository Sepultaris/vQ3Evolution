/* Adaptive exposure over the path-traced HDR. A reduction pass reads the
 * freshest HDR sources available at the end of each recorded frame (native
 * accumulated/specular/transmission/emission, or the Ray Reconstruction noisy
 * buffer after rr_pack) and appends one vec2(sumLog2, count) per workgroup to
 * this host-coherent storage buffer. The CPU reads it at the PT_0 profile
 * point, after the render fence of the previous queue submission completed,
 * and advances an EMA of the geometric-mean log2 luminance; c[19]/SL exposure
 * is the clamped target divided by the resulting 2^mean. Reference rendering
 * keeps the fixed manual exposure. Self-contained: owns its own host-visible
 * buffer and descriptor writes into the native and RR descriptor sets. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#define PT_ADAPTIVE_LOG_MIN -80.0
#define PT_ADAPTIVE_LOG_MAX 80.0

static struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    float *mapped;
    VkDeviceSize size;
    uint32_t groups_x, groups_y;
    VkPipeline pipeline;      /* Native one-set reduction (pt.layout). */
    VkPipeline rr_pipeline;   /* Two-set RR reduction (pt_rr.layout). */
    qboolean ready, rr_ready, pending, failed;
    int last_ms;
    int log_ms;
    uint32_t read_count, dispatch_count, dispatch_rr_count, drain_count;
    float log_avg;
    float exposure;
    float last_avg_log2, last_coverage, last_nan;
    uint32_t last_occ, last_full;
    int accept_ms;
    double base_log;
    qboolean base_sealed;
} pt_exposure;

static FILE *pt_exposure_log;

static void pt_exposure_logf(const char *fmt, ...)
{
    if (!pt_exposure_log) {
        pt_exposure_log = fopen("pt_adaptive.log", "ab");
        if (pt_exposure_log) fprintf(pt_exposure_log, "PT_ADAPTIVE log start\n");
    }
    if (pt_exposure_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(pt_exposure_log, fmt, args);
        va_end(args);
        fflush(pt_exposure_log);
    }
}

static void pt_exposure_logline(const char *fmt, ...)
{
    char line[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    pt_exposure_logf("%s\n", line);
}

static void pt_exposure_shutdown(void)
{
    if (pt_exposure_log) { fclose(pt_exposure_log); pt_exposure_log = NULL; }
    if (pt_exposure.rr_pipeline) qvkDestroyPipeline(vk.device, pt_exposure.rr_pipeline, NULL);
    if (pt_exposure.pipeline) qvkDestroyPipeline(vk.device, pt_exposure.pipeline, NULL);
    if (pt_exposure.mapped) qvkUnmapMemory(vk.device, pt_exposure.memory);
    if (pt_exposure.buffer) qvkDestroyBuffer(vk.device, pt_exposure.buffer, NULL);
    if (pt_exposure.memory) qvkFreeMemory(vk.device, pt_exposure.memory, NULL);
    memset(&pt_exposure, 0, sizeof(pt_exposure));
}

static qboolean pt_exposure_initialize(void)
{
    if (pt_exposure.ready) return qtrue;
    if (pt_exposure.failed) return qfalse;
    pt_exposure.groups_x = (pt.width + 7) / 8;
    pt_exposure.groups_y = (pt.height + 7) / 8;
    pt_exposure.size = ((VkDeviceSize)pt_exposure.groups_x * pt_exposure.groups_y) * 4 * sizeof(float);
    VkBufferCreateInfo buffer = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    buffer.size = pt_exposure.size;
    if (qvkCreateBuffer(vk.device, &buffer, NULL, &pt_exposure.buffer) != VK_SUCCESS) goto fail;
    VkMemoryRequirements requirements;
    VkPhysicalDeviceMemoryProperties memory;
    qvkGetBufferMemoryRequirements(vk.device, pt_exposure.buffer, &requirements);
    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            type = i;
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) break;
        }
    }
    if (type == UINT32_MAX) goto fail;
    VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type };
    if (qvkAllocateMemory(vk.device, &allocation, NULL, &pt_exposure.memory) != VK_SUCCESS ||
        qvkBindBufferMemory(vk.device, pt_exposure.buffer, pt_exposure.memory, 0) != VK_SUCCESS ||
        qvkMapMemory(vk.device, pt_exposure.memory, 0, buffer.size, 0,
            (void **)&pt_exposure.mapped) != VK_SUCCESS) goto fail;
    VkDescriptorBufferInfo info = { pt_exposure.buffer, 0, pt_exposure.size };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = pt.set, .dstBinding = 49, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &info };
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    extern unsigned char pt_exposure_comp_spv[];
    extern int pt_exposure_comp_spv_size;
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = pt_exposure_comp_spv_size, .pCode = (const uint32_t *)pt_exposure_comp_spv };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pt.layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" } };
    VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt_exposure.pipeline);
    qvkDestroyShaderModule(vk.device, module, NULL);
    if (result != VK_SUCCESS) goto fail;
    /* Seed the EMA so enabling adaptive exposure keeps the current exposure. */
    double manual = r_pathTracingExposure->value;
    if (manual <= 0.0001) manual = 1.0;
    double target = r_pathTracingAdaptiveTarget->value;
    if (target <= 0.0) target = 0.18;
    pt_exposure.log_avg = (float)((log(target) - log(manual)) / log(2.0));
    pt_exposure.exposure = (float)manual;
    pt_exposure.base_log = 0.0;
    pt_exposure.base_sealed = qfalse;
    pt_exposure.ready = qtrue;
    pt_exposure_logline("PT_ADAPTIVE initialized: buffer=%.0fKiB groups=%ux%u seed_manual=%.3f seed_exposure=%.3f",
        (double)pt_exposure.size / 1024.0, pt_exposure.groups_x, pt_exposure.groups_y, manual, pt_exposure.exposure);
    return qtrue;
fail:
    pt_exposure_shutdown();
    pt_exposure.failed = qtrue;
    pt_exposure_logline("PT_ADAPTIVE_UNAVAILABLE: reduction resources/pipeline unavailable; manual exposure retained");
    return qfalse;
}

static qboolean pt_exposure_rr_initialize(void)
{
    if (!pt_exposure.ready) return qfalse;
    if (pt_exposure.rr_ready) return qtrue;
    if (!pt_rr.set_layout || !pt_rr.pool || !pt_rr.layout) return qfalse;
    extern unsigned char pt_exposure_rr_comp_spv[];
    extern int pt_exposure_rr_comp_spv_size;
    VkDescriptorBufferInfo info = { pt_exposure.buffer, 0, pt_exposure.size };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = pt_rr.set, .dstBinding = 14, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &info };
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = pt_exposure_rr_comp_spv_size, .pCode = (const uint32_t *)pt_exposure_rr_comp_spv };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) return qfalse;
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pt_rr.layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" } };
    VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt_exposure.rr_pipeline);
    qvkDestroyShaderModule(vk.device, module, NULL);
    if (result != VK_SUCCESS) return qfalse;
    pt_exposure.rr_ready = qtrue;
    return qtrue;
}

static qboolean pt_exposure_enabled(void)
{
    /* Reference rendering holds a fixed manual exposure for convergence A/B. */
    return r_pathTracingAutoExposure->integer != 0 && !r_pathTracingReference->integer;
}

static float pt_exposure_current(void)
{
    if (pt_exposure_enabled() && pt_exposure.ready) return pt_exposure.exposure;
    return r_pathTracingExposure->value;
}

static void pt_exposure_dispatch(VkCommandBuffer cmd, const float *push)
{
    if (!pt_exposure.ready && !pt_exposure_initialize()) return;
    qboolean rr = pt_rr.frame_ready;
    ++pt_exposure.dispatch_count;
    if (rr) ++pt_exposure.dispatch_rr_count;
    VkDescriptorSet sets[2];
    uint32_t set_count;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    if (rr) {
        if (!pt_exposure_rr_initialize()) return;
        layout = pt_rr.layout;
        pipeline = pt_exposure.rr_pipeline;
        sets[0] = pt.set; sets[1] = pt_rr.set; set_count = 2;
    } else {
        layout = pt.layout;
        pipeline = pt_exposure.pipeline;
        sets[0] = pt.set; set_count = 1;
    }
    /* No generic barrier stands between the trace/guides and this read when the
     * shader-profile instrumentation is disabled, so order the HDR dependency
     * explicitly before the reduction. */
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, set_count, sets, 0, NULL);
    qvkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, push);
    qvkCmdDispatch(cmd, pt_exposure.groups_x, pt_exposure.groups_y, 1);
    VkMemoryBarrier host = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &host, 0, NULL, 0, NULL);
    pt_exposure.pending = qtrue;
}

static void pt_exposure_read(void)
{
    int now = ri.Milliseconds();
    ++pt_exposure.read_count;
    if (now - pt_exposure.log_ms >= 500) {
        pt_exposure.log_ms = now;
        pt_exposure_logline("PT_ADAPTIVE state: enabled=%d ref=%d ready=%d pending=%d rr_ready=%d mapped=%d manual=%.3f exposure=%.3f avg_log2=%.3f coverage=%.0f nan=%.0f occ=%u groups=%ux%u reads=%u dispatches=%u rr_d=%u frame_ready=%d drain=%u",
            pt_exposure_enabled(), r_pathTracingReference->integer, pt_exposure.ready, pt_exposure.pending,
            pt_exposure.rr_ready, pt_exposure.mapped != NULL, r_pathTracingExposure->value, pt_exposure.exposure,
            pt_exposure.last_avg_log2, pt_exposure.last_coverage, pt_exposure.last_nan, pt_exposure.last_occ, pt_exposure.groups_x, pt_exposure.groups_y,
            pt_exposure.read_count, pt_exposure.dispatch_count, pt_exposure.dispatch_rr_count, pt_rr.frame_ready,
            pt_exposure.drain_count);
    }
    if (!pt_exposure.pending || !pt_exposure.mapped) return;
    pt_exposure.pending = qfalse;
    double sum = 0, coverage = 0, nan_lanes = 0;
    uint32_t groups = pt_exposure.groups_x * pt_exposure.groups_y;
    uint32_t occ = 0, full = 0;
    const float *p = pt_exposure.mapped;
    for (uint32_t i = 0; i < groups; ++i) {
        float lum = p[i * 4], weight = p[i * 4 + 1], nan_l = p[i * 4 + 2];
        nan_lanes += nan_l;
        if (weight > 0.0f) ++occ;
        if (weight >= 63.0f) ++full;
        if (weight > 0.0f && lum >= 0.0f && lum <= 1e30f) {
            sum += lum;
            coverage += weight;
            ++pt_exposure.drain_count;
        }
    }
    /* Debug harness for the delivery failure: stay open below the old 50%
     * dispatch threshold, record every accepted measurement at 1 Hz. */
    if (coverage < 64.0) return;
    /* The per-frame clock advances whether or not a measurement is accepted
     * (the EMA is frame-rate anchored), but a long gate/unlock or a stall in
     * the read must not collapse several seconds of lag into one snap: clamp
     * the interval to the smoothing time constant, whose own step then
     * converges over subsequent frames. */
    double dt = pt_exposure.last_ms > 0 ? (double)(now - pt_exposure.last_ms) * 0.001 : 1.0 / 60.0;
    if (dt > 1.0 / 30.0) dt = 1.0 / 30.0;
    pt_exposure.last_ms = now;
    double tau = r_pathTracingAdaptiveSpeed->value;
    if (tau < 0.01) tau = 0.25;
    double alpha = 1.0 - exp(-dt / tau);
    double lum_mean = sum / coverage;
    double ma = log2(lum_mean);
    if (now - pt_exposure.accept_ms >= 1000 || pt_exposure.accept_ms == 0) {
        pt_exposure.accept_ms = now;
        double fast = pt_exposure.log_avg + (ma - pt_exposure.log_avg) * alpha;
        double base = pt_exposure.base_sealed ? pt_exposure.base_log :
            pt_exposure.base_log + (ma - pt_exposure.base_log) * (1.0 - exp(-dt / 30.0));
        pt_exposure_logline("PT_ADAPTIVE accept: frame_mean=%.3f mean_lum=%.4f coverage=%.0f occ=%u full=%u nan=%.0f base(%.3f) ema(%.3f->%.3f) exposure(%.3f) dt=%.3f",
            ma, lum_mean, coverage, occ, full, nan_lanes, base, pt_exposure.log_avg, fast,
            pt_exposure.exposure, dt);
    }
    pt_exposure.log_avg = (float)(pt_exposure.log_avg + (ma - pt_exposure.log_avg) * alpha);
    if (!pt_exposure.base_sealed) {
        pt_exposure.base_log = ma;
        pt_exposure.base_sealed = qtrue;
    } else {
        pt_exposure.base_log += (ma - pt_exposure.base_log) * (1.0 - exp(-dt / 30.0));
    }
    double manual = r_pathTracingExposure->value;
    if (manual <= 0.0001) manual = 1.0;
    double target = r_pathTracingAdaptiveTarget->value;
    if (target <= 0.0) target = 0.18;
    double exposure = manual * exp((pt_exposure.base_log - pt_exposure.log_avg) * log(2.0)) * (target / 0.18);
    double min_exposure = r_pathTracingAdaptiveMin->value;
    double max_exposure = r_pathTracingAdaptiveMax->value;
    if (min_exposure > max_exposure) { double t = min_exposure; min_exposure = max_exposure; max_exposure = t; }
    if (exposure < min_exposure) exposure = min_exposure;
    if (exposure > max_exposure) exposure = max_exposure;
    pt_exposure.exposure = (float)exposure;
    pt_exposure.last_avg_log2 = (float)ma;
    pt_exposure.last_coverage = (float)coverage;
    pt_exposure.last_nan = (float)nan_lanes;
    pt_exposure.last_occ = occ;
    pt_exposure.last_full = full;
}