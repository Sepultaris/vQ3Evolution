/* Local Vulkan post effects. No world/material, RR or mod-VM changes.
 * Ordered color passes, reduced blur targets and read-only depth, GPL-2.0-or-later. */
#include "vk_postfx.h"
#include "vk_instance.h"
#include "vk_cmd.h"
#include "vk_image.h"
#include "ref_import.h"
#include "image_loader.h"
#include "../renderercommon/postfx_parse.h"
#include "../renderercommon/postfx_spirv.h"

typedef struct { VkPipeline pipeline; VkDescriptorSet set; } effectPass_t;
typedef struct {
    postfxEffect_t info;
    cvar_t *enabled,*order,*params[PFX_MAX_PARAMS];
    effectPass_t passes[PFX_MAX_PASSES];
    postfxImage_t texture;
} effect_t;
typedef struct {
    float extentTimeFrame[4],params[PFX_MAX_PARAMS],projectionJitter[4];
    float motionInfo[4],previousClipRows[3][4];
} effectPush_t; // 128 bytes: fits Vulkan's minimum push-constant guarantee.
static struct {
    effect_t effects[PFX_MAX_EFFECTS];
    int count;
    uint32_t width,height;
    cvar_t *enabled;
    VkDescriptorSetLayout setLayout;
    VkDescriptorPool pool;
    VkPipelineLayout layout;
    VkRenderPass renderPass[2]; // Full-size RGBA8 / quarter-size RGBA16F.
    VkFramebuffer framebuffer[4];
    VkSampler sampler,depthSampler;
    postfxImage_t image[4];
} fx;

qboolean vk_postfx_enabled(void) { return fx.enabled && fx.enabled->integer; }
qboolean vk_postfx_get_effect(int index,postfxEffect_t *effect) {
    if (index<0 || index>=fx.count || !effect) return qfalse;
    *effect=fx.effects[index].info; return qtrue;
}
void vk_postfx_info_f(void) {
    ri.Printf(PRINT_ALL,"PostFX: %d packages, master %s, %ux%u; after NV, before HUD\n",fx.count,
        vk_postfx_enabled() ? "on":"off",fx.width,fx.height);
    for (int i=0;i<fx.count;++i) {
        effect_t *e=&fx.effects[i];
        ri.Printf(PRINT_ALL,"  %s: enabled %d, order %d, %d passes: %s\n",e->info.id,
            e->enabled ? e->enabled->integer:0,e->order ? e->order->integer:0,e->info.numPasses,e->info.status);
    }
}
void vk_postfx_reload_f(void) {
    ri.Printf(PRINT_ALL,"PostFX: reloading local packages with a deferred renderer restart\n");
    ri.RequestVideoRestart(0);
}
void vk_postfx_shutdown(void) {
    for (int i=0;i<fx.count;++i) for (int p=0;p<fx.effects[i].info.numPasses;++p)
        if (fx.effects[i].passes[p].pipeline) qvkDestroyPipeline(vk.device,fx.effects[i].passes[p].pipeline,NULL);
    if (fx.pool) qvkDestroyDescriptorPool(vk.device,fx.pool,NULL);
    for (int i=0;i<fx.count;++i) {
        postfxImage_t *im=&fx.effects[i].texture;
        if (im->view) qvkDestroyImageView(vk.device,im->view,NULL);
        if (im->image) qvkDestroyImage(vk.device,im->image,NULL);
        if (im->memory) qvkFreeMemory(vk.device,im->memory,NULL);
    }
    if (fx.layout) qvkDestroyPipelineLayout(vk.device,fx.layout,NULL);
    if (fx.setLayout) qvkDestroyDescriptorSetLayout(vk.device,fx.setLayout,NULL);
    if (fx.sampler) qvkDestroySampler(vk.device,fx.sampler,NULL);
    if (fx.depthSampler) qvkDestroySampler(vk.device,fx.depthSampler,NULL);
    for (int i=0;i<4;++i) {
        if (fx.framebuffer[i]) qvkDestroyFramebuffer(vk.device,fx.framebuffer[i],NULL);
        if (fx.image[i].view) qvkDestroyImageView(vk.device,fx.image[i].view,NULL);
        if (fx.image[i].image) qvkDestroyImage(vk.device,fx.image[i].image,NULL);
        if (fx.image[i].memory) qvkFreeMemory(vk.device,fx.image[i].memory,NULL);
    }
    for (int i=0;i<2;++i) if (fx.renderPass[i]) qvkDestroyRenderPass(vk.device,fx.renderPass[i],NULL);
    memset(&fx,0,sizeof(fx));
}

static qboolean create_targets(void) {
    int targets=2;
    VkAttachmentDescription attachment={0};
    VkAttachmentReference ref={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={0};
    VkRenderPassCreateInfo rp={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    attachment.format=VK_FORMAT_R8G8B8A8_UNORM;
    attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount=1; subpass.pColorAttachments=&ref;
    rp.attachmentCount=1; rp.pAttachments=&attachment; rp.subpassCount=1; rp.pSubpasses=&subpass;
    if (qvkCreateRenderPass(vk.device,&rp,NULL,&fx.renderPass[0])!=VK_SUCCESS) return qfalse;
    for (int i=0;i<fx.count;++i) if (fx.effects[i].info.downsample) targets=4;
    if (targets==4) {
        VkFormatProperties props;
        VkFormatFeatureFlags required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        qvkGetPhysicalDeviceFormatProperties(vk.physical_device,VK_FORMAT_R16G16B16A16_SFLOAT,&props);
        if ((props.optimalTilingFeatures&required)!=required) return qfalse;
        attachment.format=VK_FORMAT_R16G16B16A16_SFLOAT;
        if (qvkCreateRenderPass(vk.device,&rp,NULL,&fx.renderPass[1])!=VK_SUCCESS) return qfalse;
    }
    for (int i=0;i<targets;++i) {
        postfxImage_t *im=&fx.image[i];
        VkImageCreateInfo ci={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        VkMemoryRequirements req;
        VkMemoryAllocateInfo alloc={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        VkImageViewCreateInfo vi={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        VkFramebufferCreateInfo fb={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        VkPhysicalDeviceMemoryProperties memory;
        uint32_t type;
        ci.imageType=VK_IMAGE_TYPE_2D; ci.format=i<2 ? VK_FORMAT_R8G8B8A8_UNORM:VK_FORMAT_R16G16B16A16_SFLOAT;
        ci.extent.width=i<2 ? fx.width:(fx.width+3)/4;
        ci.extent.height=i<2 ? fx.height:(fx.height+3)/4; ci.extent.depth=1;
        ci.mipLevels=ci.arrayLayers=1; ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (i>=2) ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (qvkCreateImage(vk.device,&ci,NULL,&im->image)!=VK_SUCCESS) return qfalse;
        qvkGetImageMemoryRequirements(vk.device,im->image,&req);
        qvkGetPhysicalDeviceMemoryProperties(vk.physical_device,&memory);
        for (type=0;type<memory.memoryTypeCount;++type)
            if ((req.memoryTypeBits&(1u<<type)) && (memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
        if (type==memory.memoryTypeCount) return qfalse;
        alloc.allocationSize=req.size; alloc.memoryTypeIndex=type;
        if (qvkAllocateMemory(vk.device,&alloc,NULL,&im->memory)!=VK_SUCCESS ||
            qvkBindImageMemory(vk.device,im->image,im->memory,0)!=VK_SUCCESS) return qfalse;
        vi.image=im->image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=ci.format;
        vi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; vi.subresourceRange.levelCount=vi.subresourceRange.layerCount=1;
        if (qvkCreateImageView(vk.device,&vi,NULL,&im->view)!=VK_SUCCESS) return qfalse;
        im->format=ci.format; im->usage=ci.usage; im->aspect=VK_IMAGE_ASPECT_COLOR_BIT;
        fb.renderPass=fx.renderPass[i<2 ? 0:1]; fb.attachmentCount=1; fb.pAttachments=&im->view;
        fb.width=ci.extent.width; fb.height=ci.extent.height; fb.layers=1;
        if (qvkCreateFramebuffer(vk.device,&fb,NULL,&fx.framebuffer[i])!=VK_SUCCESS) return qfalse;
    }
    return qtrue;
}
/* Independent color upload: no world-texture picmip, gamma/light scaling or
 * 2048-pixel material limit. Only called during renderer initialization. */
static qboolean load_texture(effect_t *e) {
    byte *pixels=NULL; int width=0,height=0; qboolean ok=qfalse;
    postfxImage_t *im=&e->texture;
    VkBuffer staging=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkCommandBuffer cmd=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE; void *mapped=NULL;
    VkPhysicalDeviceProperties properties;
    VkMemoryRequirements req; VkPhysicalDeviceMemoryProperties types;
    VkMemoryAllocateInfo allocation={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkImageCreateInfo image={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkBufferCreateInfo buffer={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    VkCommandPoolCreateInfo pc={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    VkCommandBufferAllocateInfo ca={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkFenceCreateInfo fc={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkBufferImageCopy copy={0};
    if (!e->info.texture[0]) return qtrue;
    R_LoadPostFXPNG(e->info.texture,&pixels,&width,&height);
    if (!pixels || width<1 || height<1) goto done;
    qvkGetPhysicalDeviceProperties(vk.physical_device,&properties);
    if ((uint32_t)width>properties.limits.maxImageDimension2D || (uint32_t)height>properties.limits.maxImageDimension2D) goto done;
    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device,&types);
    image.imageType=VK_IMAGE_TYPE_2D; image.format=VK_FORMAT_R8G8B8A8_UNORM;
    image.extent.width=width; image.extent.height=height; image.extent.depth=1;
    image.mipLevels=image.arrayLayers=1; image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;
    image.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
    if (qvkCreateImage(vk.device,&image,NULL,&im->image)!=VK_SUCCESS) goto done;
    qvkGetImageMemoryRequirements(vk.device,im->image,&req);
    allocation.memoryTypeIndex=types.memoryTypeCount;
    for (uint32_t i=0;i<types.memoryTypeCount;++i)
        if ((req.memoryTypeBits&(1u<<i)) && (types.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { allocation.memoryTypeIndex=i; break; }
    allocation.allocationSize=req.size;
    if (allocation.memoryTypeIndex==types.memoryTypeCount || qvkAllocateMemory(vk.device,&allocation,NULL,&im->memory)!=VK_SUCCESS ||
        qvkBindImageMemory(vk.device,im->image,im->memory,0)!=VK_SUCCESS) goto done;
    view.image=im->image; view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=image.format;
    view.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount=view.subresourceRange.layerCount=1;
    if (qvkCreateImageView(vk.device,&view,NULL,&im->view)!=VK_SUCCESS) goto done;
    buffer.size=(VkDeviceSize)width*height*4; buffer.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (qvkCreateBuffer(vk.device,&buffer,NULL,&staging)!=VK_SUCCESS) goto done;
    qvkGetBufferMemoryRequirements(vk.device,staging,&req);
    allocation.allocationSize=req.size; allocation.memoryTypeIndex=types.memoryTypeCount;
    for (uint32_t i=0;i<types.memoryTypeCount;++i) {
        const VkMemoryPropertyFlags host=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((req.memoryTypeBits&(1u<<i)) && (types.memoryTypes[i].propertyFlags&host)==host) { allocation.memoryTypeIndex=i; break; }
    }
    if (allocation.memoryTypeIndex==types.memoryTypeCount || qvkAllocateMemory(vk.device,&allocation,NULL,&memory)!=VK_SUCCESS ||
        qvkBindBufferMemory(vk.device,staging,memory,0)!=VK_SUCCESS ||
        qvkMapMemory(vk.device,memory,0,buffer.size,0,&mapped)!=VK_SUCCESS) goto done;
    memcpy(mapped,pixels,(size_t)buffer.size); qvkUnmapMemory(vk.device,memory); mapped=NULL;
    pc.queueFamilyIndex=vk.queue_family_index; pc.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (qvkCreateCommandPool(vk.device,&pc,NULL,&pool)!=VK_SUCCESS) goto done;
    ca.commandPool=pool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1;
    if (qvkAllocateCommandBuffers(vk.device,&ca,&cmd)!=VK_SUCCESS) goto done;
    begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (qvkBeginCommandBuffer(cmd,&begin)!=VK_SUCCESS) goto done;
    record_image_layout_transition(cmd,im->image,VK_IMAGE_ASPECT_COLOR_BIT,0,VK_IMAGE_LAYOUT_UNDEFINED,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount=1;
    copy.imageExtent=image.extent;
    qvkCmdCopyBufferToImage(cmd,staging,im->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    record_image_layout_transition(cmd,im->image,VK_IMAGE_ASPECT_COLOR_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (qvkEndCommandBuffer(cmd)!=VK_SUCCESS || qvkCreateFence(vk.device,&fc,NULL,&fence)!=VK_SUCCESS) goto done;
    submit.commandBufferCount=1; submit.pCommandBuffers=&cmd;
    if (qvkQueueSubmit(vk.queue,1,&submit,fence)!=VK_SUCCESS) goto done;
    if (qvkWaitForFences(vk.device,1,&fence,VK_TRUE,UINT64_MAX)!=VK_SUCCESS) goto done;
    im->format=image.format; im->layout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    im->aspect=VK_IMAGE_ASPECT_COLOR_BIT; im->usage=image.usage; ok=qtrue;
    ri.Printf(PRINT_ALL,"PostFX: %s texture %s loaded at %dx%d\n",e->info.id,e->info.texture,width,height);
done:
    if (pixels) free(pixels);
    if (mapped) qvkUnmapMemory(vk.device,memory);
    if (fence) qvkDestroyFence(vk.device,fence,NULL);
    if (pool) qvkDestroyCommandPool(vk.device,pool,NULL);
    if (staging) qvkDestroyBuffer(vk.device,staging,NULL);
    if (memory) qvkFreeMemory(vk.device,memory,NULL);
    if (!ok) {
        if (im->view) qvkDestroyImageView(vk.device,im->view,NULL);
        if (im->image) qvkDestroyImage(vk.device,im->image,NULL);
        if (im->memory) qvkFreeMemory(vk.device,im->memory,NULL);
        memset(im,0,sizeof(*im));
    }
    return ok;
}
static qboolean create_layout(void) {
    const VkShaderStageFlags stages=VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding bindings[6]={{0}};
    VkDescriptorSetLayoutCreateInfo si={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    VkPushConstantRange push={stages,0,sizeof(effectPush_t)};
    VkPipelineLayoutCreateInfo pi={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,5*PFX_MAX_EFFECTS*PFX_MAX_PASSES},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,PFX_MAX_EFFECTS*PFX_MAX_PASSES}};
    VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    VkSamplerCreateInfo sampler={VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount=1; bindings[0].stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding=1; bindings[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount=1; bindings[1].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2]=bindings[0]; bindings[2].binding=2;
    bindings[3]=bindings[0]; bindings[3].binding=3;
    bindings[4]=bindings[0]; bindings[4].binding=4;
    bindings[5]=bindings[0]; bindings[5].binding=5;
    si.bindingCount=6; si.pBindings=bindings;
    if (qvkCreateDescriptorSetLayout(vk.device,&si,NULL,&fx.setLayout)!=VK_SUCCESS) return qfalse;
    pi.setLayoutCount=1; pi.pSetLayouts=&fx.setLayout; pi.pushConstantRangeCount=1; pi.pPushConstantRanges=&push;
    if (qvkCreatePipelineLayout(vk.device,&pi,NULL,&fx.layout)!=VK_SUCCESS) return qfalse;
    pool.maxSets=PFX_MAX_EFFECTS*PFX_MAX_PASSES; pool.poolSizeCount=2; pool.pPoolSizes=sizes;
    if (qvkCreateDescriptorPool(vk.device,&pool,NULL,&fx.pool)!=VK_SUCCESS) return qfalse;
    sampler.magFilter=sampler.minFilter=VK_FILTER_LINEAR;
    sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (qvkCreateSampler(vk.device,&sampler,NULL,&fx.sampler)!=VK_SUCCESS) return qfalse;
    // Depth formats do not necessarily support linear filtering. Point sampling
    // also avoids inventing intermediate depths at silhouettes.
    sampler.magFilter=sampler.minFilter=VK_FILTER_NEAREST;
    return qvkCreateSampler(vk.device,&sampler,NULL,&fx.depthSampler)==VK_SUCCESS;
}
/* Reject truncation, wrong stages, non-main entry points and non-8x8 kernels.
 * This is an envelope check, NOT a replacement for spirv-val or a GPU sandbox. */
static VkShaderModule load_module(const char *file,uint32_t model,qboolean texture,qboolean original,qboolean depth,qboolean motion) {
    void *data=NULL; long size=ri.PostFX_Read(file,&data);
    VkShaderModule module=VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    if (size<20 || size>1024*1024 || (size&3) || !PFX_SpirvGuides(data,(size_t)size/4,model,texture,original,depth,motion)) goto done;
    ci.codeSize=size; ci.pCode=data;
    if (qvkCreateShaderModule(vk.device,&ci,NULL,&module)!=VK_SUCCESS) module=VK_NULL_HANDLE;
done:
    if (data) ri.PostFX_Free(data);
    if (!module) ri.Printf(PRINT_WARNING,"PostFX: rejected or missing shader %s\n",file);
    return module;
}
static qboolean create_pipeline(effect_t *e,int index) {
    postfxPass_t *p=&e->info.passes[index]; effectPass_t *out=&e->passes[index];
    VkShaderModule shader=load_module(p->shader,p->compute ? 5:4,e->info.texture[0]!=0,e->info.downsample!=0,e->info.depth!=0,e->info.motion!=0),vertex=VK_NULL_HANDLE;
    VkResult result=VK_ERROR_INITIALIZATION_FAILED;
    VkDescriptorSetAllocateInfo set={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    if (!shader) return qfalse;
    if (p->compute) {
        VkComputePipelineCreateInfo ci={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module=shader; ci.stage.pName="main";
        ci.layout=fx.layout;
        result=qvkCreateComputePipelines(vk.device,VK_NULL_HANDLE,1,&ci,NULL,&out->pipeline);
    } else {
        VkGraphicsPipelineCreateInfo ci={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
        VkPipelineVertexInputStateCreateInfo input={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        VkPipelineViewportStateCreateInfo vp={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        VkPipelineMultisampleStateCreateInfo samples={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState attachment={0};
        VkPipelineColorBlendStateCreateInfo blend={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        int divisor=PFX_PassDivisor(&e->info,index);
        uint32_t width=(fx.width+divisor-1)/divisor,height=(fx.height+divisor-1)/divisor;
        VkViewport viewport={0,0,(float)width,(float)height,0,1};
        VkRect2D scissor={{0,0},{width,height}};
        vertex=load_module(p->vertex,0,qfalse,qfalse,qfalse,qfalse); if (!vertex) goto done;
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT; stages[0].module=vertex; stages[0].pName="main";
        stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module=shader; stages[1].pName="main";
        assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        vp.viewportCount=vp.scissorCount=1; vp.pViewports=&viewport; vp.pScissors=&scissor;
        raster.polygonMode=VK_POLYGON_MODE_FILL; raster.cullMode=VK_CULL_MODE_NONE; raster.lineWidth=1;
        samples.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        attachment.colorWriteMask=15; blend.attachmentCount=1; blend.pAttachments=&attachment;
        ci.stageCount=2; ci.pStages=stages; ci.pVertexInputState=&input; ci.pInputAssemblyState=&assembly;
        ci.pViewportState=&vp; ci.pRasterizationState=&raster; ci.pMultisampleState=&samples;
        ci.pColorBlendState=&blend; ci.layout=fx.layout; ci.renderPass=fx.renderPass[divisor==1 ? 0:1];
        result=qvkCreateGraphicsPipelines(vk.device,VK_NULL_HANDLE,1,&ci,NULL,&out->pipeline);
    }
done:
    qvkDestroyShaderModule(vk.device,shader,NULL);
    if (vertex) qvkDestroyShaderModule(vk.device,vertex,NULL);
    if (result!=VK_SUCCESS) return qfalse;
    set.descriptorPool=fx.pool; set.descriptorSetCount=1; set.pSetLayouts=&fx.setLayout;
    return qvkAllocateDescriptorSets(vk.device,&set,&out->set)==VK_SUCCESS;
}
void vk_postfx_initialize(uint32_t width,uint32_t height) {
    char names[PFX_MAX_EFFECTS][PFX_ID]; int count;
    memset(&fx,0,sizeof(fx)); fx.width=width; fx.height=height;
    fx.enabled=ri.Cvar_Get("r_postfx","0",CVAR_ARCHIVE|CVAR_LATCH);
    ri.Cvar_CheckRange(fx.enabled,0,1,qtrue);
    ri.Cvar_SetDescription(fx.enabled,"Local post-effect chain after reconstruction/NV and before HUD. Restart to apply; postfx_info lists packages.");
    count=ri.PostFX_List(names);
    for (int i=0;i<count;++i) {
        char file[PFX_FILE],name[96]; void *data=NULL; effect_t *e=&fx.effects[fx.count];
        Com_sprintf(file,sizeof(file),"%s.effect",names[i]);
        long length=ri.PostFX_Read(file,&data);
        if (length<=0) continue;
        qboolean parsed=length<=16384 && !memchr(data,0,length) && PFX_Parse(names[i],data,&e->info);
        ri.PostFX_Free(data);
        if (!parsed) { ri.Printf(PRINT_WARNING,"PostFX: invalid manifest %s; skipped\n",file); continue; }
        Com_sprintf(name,sizeof(name),"r_fx_%s_enabled",e->info.id);
        e->enabled=ri.Cvar_Get(name,"0",CVAR_ARCHIVE); ri.Cvar_CheckRange(e->enabled,0,1,qtrue);
        Com_sprintf(name,sizeof(name),"r_fx_%s_order",e->info.id);
        e->order=ri.Cvar_Get(name,"50",CVAR_ARCHIVE); ri.Cvar_CheckRange(e->order,0,100,qtrue);
        for (int p=0;p<e->info.numParams;++p) {
            postfxParam_t *param=&e->info.params[p]; char initial[32];
            Com_sprintf(name,sizeof(name),"r_fx_%s_%s",e->info.id,param->id);
            Com_sprintf(initial,sizeof(initial),"%.9g",param->initial);
            e->params[p]=ri.Cvar_Get(name,initial,CVAR_ARCHIVE);
            // Export the engine's actual factory default to the options panel.
            param->initial=atof(e->params[p]->resetString);
            ri.Cvar_CheckRange(e->params[p],param->minimum,param->maximum,qfalse);
        }
        Q_strncpyz(e->info.status,"Master off; enable r_postfx and restart to load",sizeof(e->info.status));
        ++fx.count;
    }
    if (!vk_postfx_enabled() || !fx.count) return;
    if (!create_targets() || !create_layout()) {
        ri.Printf(PRINT_WARNING,"PostFX: GPU resource creation failed; effects bypassed\n");
        // Keep metadata and handles for normal shutdown; no partial effect runs.
        for (int i=0;i<fx.count;++i) Q_strncpyz(fx.effects[i].info.status,"GPU resources unavailable",sizeof(fx.effects[i].info.status));
        return;
    }
    for (int i=0;i<fx.count;++i) {
        effect_t *e=&fx.effects[i]; int p;
        if (!load_texture(e)) {
            Q_strncpyz(e->info.status,"Texture missing/invalid; entire effect bypassed",sizeof(e->info.status));
            continue;
        }
        for (p=0;p<e->info.numPasses;++p) if (!create_pipeline(e,p)) break;
        e->info.ready=p==e->info.numPasses;
        Q_strncpyz(e->info.status,e->info.ready ? "Ready":"Shader/pipeline failed; entire effect bypassed",sizeof(e->info.status));
    }
    vk_postfx_info_f();
}
static int target_index(postfxImage_t *image) {
    for (int i=0;i<4;++i) if (image==&fx.image[i]) return i;
    return -1;
}
postfxImage_t *vk_postfx_record(VkCommandBuffer cmd,postfxImage_t *input,VkAccessFlags access,float time,uint32_t frame,const postfxDepth_t *depth) {
    int order[PFX_MAX_EFFECTS],n=0;
    if (!vk_postfx_enabled()) return input;
    for (int i=0;i<fx.count;++i) if (fx.effects[i].info.ready && fx.effects[i].enabled->integer) {
        if (fx.effects[i].info.depth && (!depth || !depth->image || !depth->image->view)) continue;
        if (fx.effects[i].info.motion && (!depth || !depth->motion || !depth->motion->view)) continue;
        int j=n++;
        while (j>0 && fx.effects[order[j-1]].order->integer>fx.effects[i].order->integer) { order[j]=order[j-1]; --j; }
        order[j]=i;
    }
    for (int i=0;i<n;++i) {
        effect_t *e=&fx.effects[order[i]];
        postfxImage_t *original=input;
        VkAccessFlags originalAccess=access;
        VkImageLayout originalLayout=original->layout;
        if (e->info.downsample)
            record_image_layout_transition(cmd,original->image,VK_IMAGE_ASPECT_COLOR_BIT,access,originalLayout,
                VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_GENERAL);
        if (e->info.depth)
            record_image_layout_transition(cmd,depth->image->image,depth->image->aspect,depth->access,
                depth->image->layout,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_GENERAL);
        effectPush_t push={{(float)fx.width,(float)fx.height,time,(float)(frame&0xffffff)}, {0}};
        if (e->info.motion)
            record_image_layout_transition(cmd,depth->motion->image,depth->motion->aspect,depth->motionAccess,
                depth->motion->layout,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_GENERAL);
        if (e->info.depth) memcpy(push.projectionJitter,depth->projectionJitter,sizeof(push.projectionJitter));
        if (e->info.motion) {
            memcpy(push.motionInfo,depth->motionInfo,sizeof(push.motionInfo));
            memcpy(push.projectionJitter+2,depth->motionJitter,sizeof(depth->motionJitter));
            memcpy(push.previousClipRows,depth->previousClipRows,sizeof(push.previousClipRows));
        }
        for (int p=0;p<e->info.numParams;++p) push.params[p]=e->params[p]->value;
        for (int p=0;p<e->info.numPasses;++p) {
            int target=PFX_PassTarget(&e->info,p,target_index(input),target_index(original));
            int divisor=PFX_PassDivisor(&e->info,p);
            uint32_t width=(fx.width+divisor-1)/divisor,height=(fx.height+divisor-1)/divisor;
            if (target<0) break; // Impossible for a valid manifest; never alias a sampled image.
            push.extentTimeFrame[0]=(float)width; push.extentTimeFrame[1]=(float)height;
            effectPass_t *pass=&e->passes[p]; qboolean compute=e->info.passes[p].compute;
            postfxImage_t *out=&fx.image[target];
            VkImageLayout old=input->layout;
            VkAccessFlags write=compute ? VK_ACCESS_SHADER_WRITE_BIT:VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            VkImageLayout layout=compute ? VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            VkDescriptorImageInfo infos[6]={{fx.sampler,input->view,VK_IMAGE_LAYOUT_GENERAL},
                {VK_NULL_HANDLE,out->view,VK_IMAGE_LAYOUT_GENERAL},{fx.sampler,e->texture.view,e->texture.layout},
                {fx.sampler,original->view,VK_IMAGE_LAYOUT_GENERAL},
                {fx.depthSampler,e->info.depth ? depth->image->view:VK_NULL_HANDLE,VK_IMAGE_LAYOUT_GENERAL},
                {fx.depthSampler,e->info.motion ? depth->motion->view:VK_NULL_HANDLE,VK_IMAGE_LAYOUT_GENERAL}};
            VkWriteDescriptorSet descriptors[6]={{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
            VkPipelineBindPoint bind=compute ? VK_PIPELINE_BIND_POINT_COMPUTE:VK_PIPELINE_BIND_POINT_GRAPHICS;
            if (!e->info.downsample || input!=original)
                record_image_layout_transition(cmd,input->image,VK_IMAGE_ASPECT_COLOR_BIT,access,old,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_GENERAL);
            record_image_layout_transition(cmd,out->image,VK_IMAGE_ASPECT_COLOR_BIT,
                out->layout==VK_IMAGE_LAYOUT_UNDEFINED ? 0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,
                out->layout,write,layout);
            out->layout=layout;
            for (int d=0;d<6;++d) {
                descriptors[d].dstSet=pass->set; descriptors[d].dstBinding=d; descriptors[d].descriptorCount=1;
                descriptors[d].descriptorType=d==1 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptors[d].pImageInfo=&infos[d];
            }
            // Each pass has its own set, used once per frame after the render fence.
            qvkUpdateDescriptorSets(vk.device,compute ? 2:1,descriptors,0,NULL);
            if (e->texture.view) qvkUpdateDescriptorSets(vk.device,1,&descriptors[2],0,NULL);
            if (e->info.downsample) qvkUpdateDescriptorSets(vk.device,1,&descriptors[3],0,NULL);
            if (e->info.depth) qvkUpdateDescriptorSets(vk.device,1,&descriptors[4],0,NULL);
            if (e->info.motion) qvkUpdateDescriptorSets(vk.device,1,&descriptors[5],0,NULL);
            if (!compute) {
                VkClearValue clear={{ {0,0,0,1} }};
                VkRenderPassBeginInfo begin={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                begin.renderPass=fx.renderPass[divisor==1 ? 0:1]; begin.framebuffer=fx.framebuffer[target];
                begin.renderArea.extent.width=width; begin.renderArea.extent.height=height;
                begin.clearValueCount=1; begin.pClearValues=&clear;
                qvkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
            }
            qvkCmdBindPipeline(cmd,bind,pass->pipeline);
            qvkCmdBindDescriptorSets(cmd,bind,fx.layout,0,1,&pass->set,0,NULL);
            qvkCmdPushConstants(cmd,fx.layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(push),&push);
            if (compute) qvkCmdDispatch(cmd,(width+7)/8,(height+7)/8,1);
            else { qvkCmdDraw(cmd,3,1,0,0); qvkCmdEndRenderPass(cmd); }
            // Preserve the owner's layout (including the RR and FG resource contracts).
            if (!e->info.downsample || input!=original)
                record_image_layout_transition(cmd,input->image,VK_IMAGE_ASPECT_COLOR_BIT,VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_GENERAL,access,old);
            record_image_layout_transition(cmd,out->image,VK_IMAGE_ASPECT_COLOR_BIT,write,layout,
                VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_LAYOUT_GENERAL);
            out->layout=VK_IMAGE_LAYOUT_GENERAL; input=out; access=VK_ACCESS_SHADER_READ_BIT;
        }
        if (e->info.downsample)
            record_image_layout_transition(cmd,original->image,VK_IMAGE_ASPECT_COLOR_BIT,VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,originalAccess,originalLayout);
        if (e->info.depth)
            record_image_layout_transition(cmd,depth->image->image,depth->image->aspect,VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,depth->access,depth->image->layout);
        if (e->info.motion)
            record_image_layout_transition(cmd,depth->motion->image,depth->motion->aspect,VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,depth->motionAccess,depth->motion->layout);
    }
    return input;
}
