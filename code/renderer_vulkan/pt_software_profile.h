/* Mode-1 diagnostics only. A sparse replay has its own result buffer and never
 * writes production images/history. Clock shares are not GPU milliseconds. */
typedef struct { float ticks[16]; uint32_t counts[32]; float signal[4]; } sw_profile_record_t;
typedef char sw_profile_layout_check[(sizeof(sw_profile_record_t)==208) ? 1:-1];
static struct {
    pt_buffer_t data;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    uint32_t records, frame;
    int mode;
    qboolean failed, pending, recorded;
} sw_profile;

static int sw_profile_requested(void)
{
    cvar_t *setting=ri.Cvar_Get("r_softwareRayTracingProfile","0",CVAR_TEMP);
    ri.Cvar_CheckRange(setting,0,2,qtrue);
    return pt.software ? setting->integer:0;
}

static void sw_profile_shutdown(void)
{
    if(sw_profile.pipeline) qvkDestroyPipeline(vk.device,sw_profile.pipeline,NULL);
    if(sw_profile.layout) qvkDestroyPipelineLayout(vk.device,sw_profile.layout,NULL);
    if(sw_profile.pool) qvkDestroyDescriptorPool(vk.device,sw_profile.pool,NULL);
    if(sw_profile.set_layout) qvkDestroyDescriptorSetLayout(vk.device,sw_profile.set_layout,NULL);
    if(sw_profile.data.mapped) qvkUnmapMemory(vk.device,sw_profile.data.memory);
    if(sw_profile.data.buffer) qvkDestroyBuffer(vk.device,sw_profile.data.buffer,NULL);
    if(sw_profile.data.memory) qvkFreeMemory(vk.device,sw_profile.data.memory,NULL);
    memset(&sw_profile,0,sizeof(sw_profile));
}

static qboolean sw_profile_initialize(void)
{
    int requested=sw_profile_requested();
    if(!requested) return qfalse;
    int mode=requested==1 && vk_rt_shader_clock_supported() ? 1:2;
    if(sw_profile.pipeline && sw_profile.mode==mode) return qtrue;
    if(sw_profile.failed) return qfalse;
    // Previous frame's render fence/readback has completed before recording.
    if(sw_profile.pipeline) sw_profile_shutdown();
    VkPhysicalDeviceProperties properties;
    qvkGetPhysicalDeviceProperties(vk.physical_device,&properties);
    uint32_t buffers=sw_denoise.instance ? 48:47;
    if(properties.limits.maxPerStageDescriptorStorageBuffers<buffers ||
        properties.limits.maxDescriptorSetStorageBuffers<buffers ||
        properties.limits.maxPerStageResources<PT_MAX_TEXTURES+buffers+5 ||
        properties.limits.maxBoundDescriptorSets<2) goto fail;
    sw_profile.mode=mode;
    sw_profile.records=((pt.width+63)/64)*((pt.height+63)/64)*64;
    sw_profile.data.size=(VkDeviceSize)sw_profile.records*sizeof(sw_profile_record_t);
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=sw_profile.data.size,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    if(qvkCreateBuffer(vk.device,&bi,NULL,&sw_profile.data.buffer)!=VK_SUCCESS) goto fail;
    VkMemoryRequirements requirements;
    VkPhysicalDeviceMemoryProperties memory;
    qvkGetBufferMemoryRequirements(vk.device,sw_profile.data.buffer,&requirements);
    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device,&memory);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i) {
        VkMemoryPropertyFlags flags=memory.memoryTypes[i].propertyFlags;
        VkMemoryPropertyFlags required=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if((requirements.memoryTypeBits&(1u<<i)) && (flags&required)==required) {
            type=i;
            if(flags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT) break;
        }
    }
    if(type==UINT32_MAX) goto fail;
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=type};
    if(qvkAllocateMemory(vk.device,&ai,NULL,&sw_profile.data.memory)!=VK_SUCCESS ||
        qvkBindBufferMemory(vk.device,sw_profile.data.buffer,sw_profile.data.memory,0)!=VK_SUCCESS ||
        qvkMapMemory(vk.device,sw_profile.data.memory,0,bi.size,0,&sw_profile.data.mapped)!=VK_SUCCESS) goto fail;
    VkDescriptorSetLayoutBinding binding={0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo si={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=1,.pBindings=&binding};
    if(qvkCreateDescriptorSetLayout(vk.device,&si,NULL,&sw_profile.set_layout)!=VK_SUCCESS) goto fail;
    VkDescriptorPoolSize size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets=1,.poolSizeCount=1,.pPoolSizes=&size};
    if(qvkCreateDescriptorPool(vk.device,&pi,NULL,&sw_profile.pool)!=VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo allocation={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool=sw_profile.pool,.descriptorSetCount=1,.pSetLayouts=&sw_profile.set_layout};
    if(qvkAllocateDescriptorSets(vk.device,&allocation,&sw_profile.set)!=VK_SUCCESS) goto fail;
    VkDescriptorBufferInfo info={sw_profile.data.buffer,0,bi.size};
    VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=sw_profile.set,
        .dstBinding=0,.descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&info};
    qvkUpdateDescriptorSets(vk.device,1,&write,0,NULL);
    VkDescriptorSetLayout sets[2]={pt.set_layout,sw_profile.set_layout};
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,128};
    VkPipelineLayoutCreateInfo li={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=2,.pSetLayouts=sets,.pushConstantRangeCount=1,.pPushConstantRanges=&push};
    if(qvkCreatePipelineLayout(vk.device,&li,NULL,&sw_profile.layout)!=VK_SUCCESS) goto fail;
    extern unsigned char pt_software_profile_comp_spv[],pt_software_nrd_profile_comp_spv[];
    extern unsigned char pt_software_counts_comp_spv[],pt_software_nrd_counts_comp_spv[];
    extern int pt_software_profile_comp_spv_size,pt_software_nrd_profile_comp_spv_size;
    extern int pt_software_counts_comp_spv_size,pt_software_nrd_counts_comp_spv_size;
    const unsigned char *code=mode==1 ?
        (sw_denoise.instance ? pt_software_nrd_profile_comp_spv:pt_software_profile_comp_spv):
        (sw_denoise.instance ? pt_software_nrd_counts_comp_spv:pt_software_counts_comp_spv);
    size_t code_size=mode==1 ?
        (sw_denoise.instance ? pt_software_nrd_profile_comp_spv_size:pt_software_profile_comp_spv_size):
        (sw_denoise.instance ? pt_software_nrd_counts_comp_spv_size:pt_software_counts_comp_spv_size);
    VkShaderModule module;
    VkShaderModuleCreateInfo mi={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize=code_size,.pCode=(const uint32_t *)code};
    if(qvkCreateShaderModule(vk.device,&mi,NULL,&module)!=VK_SUCCESS) goto fail;
    VkComputePipelineCreateInfo ci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=sw_profile.layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,
            .module=module,.pName="main"}};
    VkResult result=qvkCreateComputePipelines(vk.device,VK_NULL_HANDLE,1,&ci,NULL,&sw_profile.pipeline);
    qvkDestroyShaderModule(vk.device,module,NULL);
    if(result!=VK_SUCCESS) goto fail;
    if(requested==1 && mode==2)
        ri.Printf(PRINT_WARNING,"SW_SHADER_PROFILE: clock not enabled; counters only (request 1 before vid_restart for clocks)\n");
    ri.Printf(PRINT_ALL,"SW_SHADER_PROFILE_READY mode=%d nrd=%d size=%ux%u period=64 replay=1 production_writes=0\n",
        mode,sw_denoise.instance!=NULL,pt.width,pt.height);
    return qtrue;
fail:
    sw_profile_shutdown(); sw_profile.failed=qtrue;
    ri.Printf(PRINT_WARNING,"SW_SHADER_PROFILE_UNAVAILABLE: diagnostic resources/pipeline unavailable; normal tracing retained\n");
    return qfalse;
}

static void sw_profile_read(void)
{
    if(!sw_profile.pending) return;
    sw_profile.pending=qfalse;
    double ticks[16]={0},counts[32]={0};
    uint32_t pixels=0,invalid=0;
    const sw_profile_record_t *records=sw_profile.data.mapped;
    for(uint32_t i=0;i<sw_profile.records;++i) {
        const sw_profile_record_t *r=records+i;
        if(!r->counts[0]) continue;
        qboolean valid=qtrue;
        for(int j=0;j<16;++j) if(!(r->ticks[j]>=0 && r->ticks[j]<1e30f)) valid=qfalse;
        for(int j=0;j<4;++j) if(!(r->signal[j]>=-1e30f && r->signal[j]<=1e30f)) valid=qfalse;
        if(!valid) { ++invalid; continue; }
        ++pixels;
        for(int j=0;j<16;++j) ticks[j]+=r->ticks[j];
        for(int j=0;j<32;++j) counts[j]+=r->counts[j];
    }
    char row[4096];
    int length=snprintf(row,sizeof(row),"SW_SHADER_PROFILE frame=%u phase=%u mode=%d pixels=%u invalid=%u",
        sw_profile.frame,sw_profile.frame&63u,sw_profile.mode,pixels,invalid);
    for(int j=0;j<16;++j) length+=snprintf(row+length,sizeof(row)-length," t%d=%.0f",j,ticks[j]);
    for(int j=0;j<32;++j) length+=snprintf(row+length,sizeof(row)-length," c%d=%.0f",j,counts[j]);
    ri.Printf(PRINT_ALL,"%s\n",row);
}

static void sw_profile_record(VkCommandBuffer cmd,const float *push)
{
    sw_profile.recorded=qfalse;
    if(!sw_profile_requested() || !sw_profile_initialize()) return;
    memset(sw_profile.data.mapped,0,(size_t)sw_profile.data.size);
    VkMemoryBarrier before={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_SHADER_READ_BIT};
    qvkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,1,&before,0,NULL,0,NULL);
    VkDescriptorSet sets[2]={pt.set,sw_profile.set};
    qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,sw_profile.layout,0,2,sets,0,NULL);
    qvkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,sw_profile.pipeline);
    qvkCmdPushConstants(cmd,sw_profile.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,push);
    qvkCmdDispatch(cmd,(pt.width+7)/8,(pt.height+7)/8,1);
    VkMemoryBarrier after={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    qvkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,1,&after,0,NULL,0,NULL);
    qvkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pt.layout,0,1,&pt.set,0,NULL);
    qvkCmdPushConstants(cmd,pt.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,push);
    sw_profile.frame=(uint32_t)push[30];
    sw_profile.pending=sw_profile.recorded=qtrue;
}
