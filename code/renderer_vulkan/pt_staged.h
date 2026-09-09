/* Experimental split integrator, isolated from the original descriptor set,
 * layout and pipelines. No quality settings are changed on failure. */
#define PT_STAGED_STATE_STRIDE 384u
static void staged_shutdown(void)
{
    if (pt.staged.compared && pt.staged.mapped) {
        // Diagnostic only; never read host counters while GPU writers run.
        qvkDeviceWaitIdle(vk.device);
        float relative, absolute;
        memcpy(&relative, &pt.staged.mapped[3], sizeof(float));
        memcpy(&absolute, &pt.staged.mapped[4], sizeof(float));
        ri.Printf(PRINT_ALL, "PT_STAGED_COMPARE pixels=%u mismatched=%u nonfinite=%u max_relative=%g max_absolute=%g\n",
            pt.staged.mapped[0], pt.staged.mapped[1], pt.staged.mapped[2], relative, absolute);
    }
    for (int i = 0; i < 4; ++i)
        if (pt.staged.pipelines[i]) qvkDestroyPipeline(vk.device, pt.staged.pipelines[i], NULL);
    if (pt.staged.layout) qvkDestroyPipelineLayout(vk.device, pt.staged.layout, NULL);
    if (pt.staged.pool) qvkDestroyDescriptorPool(vk.device, pt.staged.pool, NULL);
    if (pt.staged.set_layout) qvkDestroyDescriptorSetLayout(vk.device, pt.staged.set_layout, NULL);
    buffer_destroy(&pt.staged.state);
    if (pt.staged.mapped) qvkUnmapMemory(vk.device, pt.staged.comparison_memory);
    if (pt.staged.comparison) qvkDestroyBuffer(vk.device, pt.staged.comparison, NULL);
    if (pt.staged.comparison_memory) qvkFreeMemory(vk.device, pt.staged.comparison_memory, NULL);
    if (pt.staged.profile_pool) pt.destroy_queries(vk.device, pt.staged.profile_pool, NULL);
    free(pt.staged.profile_ticks);
    free(pt.staged.profile_stages);
    memset(&pt.staged, 0, sizeof(pt.staged));
}

static qboolean staged_initialize(void)
{
    if (pt.staged.ready) return qtrue;
    if (pt.staged.failed) return qfalse;
    pt.staged.rows = ((uint32_t)r_pathTracingStagedRows->integer + 7u) & ~7u;
    if (!pt.staged.rows) pt.staged.rows = pt.height;
    if (pt.staged.rows > pt.height) pt.staged.rows = pt.height;
    VkDeviceSize bytes = (VkDeviceSize)pt.width * pt.staged.rows * PT_STAGED_STATE_STRIDE;
    VkPhysicalDeviceProperties properties;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    // Bound this prototype's extra residency. Unsupported sizes retain the
    // full-resolution original renderer; never silently resize or lower SPP.
    if (!bytes || bytes > properties.limits.maxStorageBufferRange || bytes > (VkDeviceSize)1024*1024*1024)
        goto fail;
    if (!buffer_create(&pt.staged.state, bytes, qfalse)) goto fail;
    VkBufferCreateInfo info = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 32, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (qvkCreateBuffer(vk.device, &info, NULL, &pt.staged.comparison) != VK_SUCCESS) goto fail;
    VkMemoryRequirements req;
    qvkGetBufferMemoryRequirements(vk.device, pt.staged.comparison, &req);
    VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    if (qvkAllocateMemory(vk.device, &allocation, NULL, &pt.staged.comparison_memory) != VK_SUCCESS ||
        qvkBindBufferMemory(vk.device, pt.staged.comparison, pt.staged.comparison_memory, 0) != VK_SUCCESS ||
        qvkMapMemory(vk.device, pt.staged.comparison_memory, 0, 32, 0, (void **)&pt.staged.mapped) != VK_SUCCESS) goto fail;
    memset(pt.staged.mapped, 0, 32);
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL},
        {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL} };
    VkDescriptorSetLayoutCreateInfo set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 2, .pBindings = bindings };
    if (qvkCreateDescriptorSetLayout(vk.device, &set_info, NULL, &pt.staged.set_layout) != VK_SUCCESS) goto fail;
    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 };
    VkDescriptorPoolCreateInfo pool_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    if (qvkCreateDescriptorPool(vk.device, &pool_info, NULL, &pt.staged.pool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo set_allocation = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pt.staged.pool, .descriptorSetCount = 1, .pSetLayouts = &pt.staged.set_layout };
    if (qvkAllocateDescriptorSets(vk.device, &set_allocation, &pt.staged.set) != VK_SUCCESS) goto fail;
    VkDescriptorBufferInfo buffers[2] = { {pt.staged.state.buffer, 0, bytes}, {pt.staged.comparison, 0, 32} };
    VkWriteDescriptorSet writes[2];
    memset(writes, 0, sizeof(writes));
    for (int i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = pt.staged.set; writes[i].dstBinding = i;
        writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &buffers[i];
    }
    qvkUpdateDescriptorSets(vk.device, 2, writes, 0, NULL);
    VkDescriptorSetLayout sets[2] = { pt.set_layout, pt.staged.set_layout };
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = sets, .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    if (qvkCreatePipelineLayout(vk.device, &layout, NULL, &pt.staged.layout) != VK_SUCCESS) goto fail;
    extern unsigned char pt_staged_0_comp_spv[], pt_staged_1_comp_spv[], pt_staged_2_comp_spv[], pt_staged_3_comp_spv[];
    extern int pt_staged_0_comp_spv_size, pt_staged_1_comp_spv_size, pt_staged_2_comp_spv_size, pt_staged_3_comp_spv_size;
    const unsigned char *code[4] = { pt_staged_0_comp_spv, pt_staged_1_comp_spv, pt_staged_2_comp_spv, pt_staged_3_comp_spv };
    const int sizes[4] = { pt_staged_0_comp_spv_size, pt_staged_1_comp_spv_size, pt_staged_2_comp_spv_size, pt_staged_3_comp_spv_size };
    VkBool32 options[3] = { VK_FALSE, VK_FALSE, VK_TRUE };
    VkSpecializationMapEntry entries[3] = { {0,0,4}, {1,4,4}, {2,8,4} };
    VkSpecializationInfo specialization = {3, entries, sizeof(options), options};
    for (int i = 0; i < 4; ++i) {
        VkShaderModule module;
        VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizes[i], .pCode = (const uint32_t *)code[i] };
        if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
        VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .flags = pipeline_statistics_flags(), .layout = pt.staged.layout,
            .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main",
                .pSpecializationInfo = &specialization } };
        int started = ri.Milliseconds();
        VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.staged.pipelines[i]);
        qvkDestroyShaderModule(vk.device, module, NULL);
        ri.Printf(PRINT_ALL, "PT_STAGED_PIPELINE stage=%d cpu_ms=%d result=%d\n", i, ri.Milliseconds()-started, result);
        if (result != VK_SUCCESS) goto fail;
        pipeline_statistics_print(pt.staged.pipelines[i], 128+i, pipeline.flags);
    }
    pt.staged.ready = qtrue;
    ri.Printf(PRINT_ALL, "PT_STAGED_READY state_bytes=%llu\n", (unsigned long long)bytes);
    return qtrue;
fail:
    staged_shutdown();
    pt.staged.failed = qtrue;
    ri.Printf(PRINT_WARNING, "PT_STAGED_UNAVAILABLE: retaining original full-quality transport\n");
    return qfalse;
}

// Per-dispatch timestamps are a DIAGNOSTIC, not an FPS benchmark. Read only
// after the existing frame fence; unavailable results never introduce a wait.
static void staged_profile_begin(VkCommandBuffer cmd, uint32_t dispatches)
{
    uint32_t count = pt.staged.profile_written;
    uint64_t *ticks = pt.staged.profile_ticks;
    if (count && pt.get_queries(vk.device, pt.staged.profile_pool, 0, count,
        count*sizeof(*ticks), ticks, sizeof(*ticks), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
        double stages[4] = {0}, sum = 0;
        for (uint32_t i = 0; i < count; i += 2) {
            double ms = ((ticks[i+1]-ticks[i]) & pt.timestamp_mask)*pt.timestamp_period/1000000.0;
            stages[pt.staged.profile_stages[i/2]] += ms;
            sum += ms;
        }
        double span = ((ticks[count-1]-ticks[0]) & pt.timestamp_mask)*pt.timestamp_period/1000000.0;
        ri.Printf(PRINT_ALL, "PT_STAGED_PROFILE rows=%u dispatches=%u init=%.3f surface=%.3f lighting=%.3f advance=%.3f gaps=%.3f span=%.3f record_cpu=%d\n",
            pt.staged.rows, count/2, stages[0], stages[1], stages[2], stages[3], span-sum, span, pt.staged.record_ms);
    }
    pt.staged.profile_written = 0;
    if (!r_pathTracingStagedProfile->integer || !pt.profile_pool) return;
    uint32_t capacity = dispatches*2;
    if (pt.staged.profile_capacity < capacity) {
        if (pt.staged.profile_pool) pt.destroy_queries(vk.device, pt.staged.profile_pool, NULL);
        free(pt.staged.profile_ticks); free(pt.staged.profile_stages);
        pt.staged.profile_pool = VK_NULL_HANDLE; pt.staged.profile_capacity = 0;
        pt.staged.profile_ticks = malloc(capacity*sizeof(uint64_t));
        pt.staged.profile_stages = malloc(dispatches);
        if (!pt.staged.profile_ticks || !pt.staged.profile_stages) return;
        PFN_vkCreateQueryPool create = (PFN_vkCreateQueryPool)qvkGetDeviceProcAddr(vk.device, "vkCreateQueryPool");
        VkQueryPoolCreateInfo info = { .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
            .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = capacity };
        if (!create || create(vk.device, &info, NULL, &pt.staged.profile_pool) != VK_SUCCESS) return;
        pt.staged.profile_capacity = capacity;
    }
    pt.reset_queries(cmd, pt.staged.profile_pool, 0, capacity);
}

static void staged_dispatch(VkCommandBuffer cmd, int stage, uint32_t rows)
{
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    qboolean profiling = r_pathTracingStagedProfile->integer && pt.profile_pool &&
        pt.staged.profile_pool && pt.staged.profile_written+2 <= pt.staged.profile_capacity;
    if (profiling) {
        pt.staged.profile_stages[pt.staged.profile_written/2] = (unsigned char)stage;
        pt.write_timestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pt.staged.profile_pool, pt.staged.profile_written++);
    }
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.staged.pipelines[stage]);
    qvkCmdDispatch(cmd, (pt.width+7)/8, (rows+7)/8, 1);
    if (profiling)
        pt.write_timestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pt.staged.profile_pool, pt.staged.profile_written++);
}

static void staged_record(VkCommandBuffer cmd, const float constants[32])
{
    float c[32];
    memcpy(c, constants, sizeof(c));
    uint32_t bands = (pt.height+pt.staged.rows-1)/pt.staged.rows;
    staged_profile_begin(cmd, bands*(1+(uint32_t)c[28]*(2*(uint32_t)c[29]+1)));
    int started = ri.Milliseconds();
    qboolean compare = r_pathTracingStaged->integer == 2 && c[31] == 0;
    // Dedicated staged shaders use this component for the first output row;
    // negative -(row+1) also requests paired validation. Restore all camera
    // constants before reconstruction. Local state indices never seed RNG.
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.staged.layout, 1, 1, &pt.staged.set, 0, NULL);
    for (uint32_t row = 0; row < pt.height; row += pt.staged.rows) {
        uint32_t rows = pt.height-row < pt.staged.rows ? pt.height-row:pt.staged.rows;
        c[7] = compare ? -(float)(row+1):(float)row;
        qvkCmdPushConstants(cmd, pt.staged.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), c);
        staged_dispatch(cmd, 0, rows);
        for (int sample = 0; sample < (int)c[28]; ++sample) {
            for (int bounce = 0; bounce < (int)c[29]; ++bounce) {
                staged_dispatch(cmd, 1, rows);
                staged_dispatch(cmd, 2, rows);
            }
            staged_dispatch(cmd, 3, rows);
        }
    }
    qvkCmdPushConstants(cmd, pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), constants);
    pt.staged.record_ms = ri.Milliseconds()-started;
    if (compare) {
        pt.staged.compared = qtrue;
        VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
        qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
    }
}
