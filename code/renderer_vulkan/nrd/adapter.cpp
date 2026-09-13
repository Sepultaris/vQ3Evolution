// Engine-owned Vulkan integration of the separately licensed NRD SDK.
// No SDK source is vendored here. See docs/SOFTWARE_DENOISING.md.
#include "software_denoiser.h"
#include <NRD.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>
#include "Prepare.h"
#include "Compose.h"
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
void require(bool ok,const char *what) { if(!ok) throw std::runtime_error(what); }
void check(VkResult result,const char *what) {
    if(result!=VK_SUCCESS) { char s[256]; std::snprintf(s,sizeof(s),"%s (Vulkan %d)",what,int(result)); throw std::runtime_error(s); }
}
struct Image { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct Pipeline { VkPipeline pipeline{}; VkPipelineLayout layout{}; std::vector<VkDescriptorSetLayout> sets; };
#define DEVICE_FUNCTIONS(X) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
 X(AllocateMemory) X(FreeMemory) X(CreateImageView) X(DestroyImageView) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
 X(CreateSampler) X(DestroySampler) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) \
 X(CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool) X(DestroyDescriptorPool) \
 X(ResetDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) X(CmdPipelineBarrier) \
 X(CmdBindDescriptorSets) X(CmdBindPipeline) X(CmdDispatch)
struct State {
    VkDevice device{};
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkPhysicalDeviceProperties properties{};
    PFN_vkGetPhysicalDeviceFormatProperties getFormat{};
    VkPhysicalDevice physical{};
#define DECLARE(name) PFN_vk##name name{};
    DEVICE_FUNCTIONS(DECLARE)
#undef DECLARE
    nrd::Instance *instance{};
    uint32_t width{},height{},stride{},slot{},frameIndex{};
    bool transitioned=false, previousValid=false;
    float previousView[16]{},previousProjection[16]{};
    VkBuffer constants{}; VkDeviceMemory constantsMemory{}; void *mapped{};
    VkDescriptorPool descriptors{};
    VkSampler samplers[2]{};
    std::vector<Image> images;
    std::vector<uint32_t> permanent,transient;
    std::vector<Pipeline> pipelines;
    Pipeline prepare,compose;
    // External signals: D, S, normal, viewZ, MV, confidence D/S, output D/S.
    enum { D,S,N,Z,MV,CD,CS,OD,OS,EXTERNALS };
    static constexpr uint32_t maxSlots=256;
    ~State() {
        auto destroyPipeline=[&](Pipeline &p) {
            if(p.pipeline) DestroyPipeline(device,p.pipeline,nullptr);
            if(p.layout) DestroyPipelineLayout(device,p.layout,nullptr);
            for(auto set:p.sets) if(set) DestroyDescriptorSetLayout(device,set,nullptr);
        };
        for(auto &p:pipelines) destroyPipeline(p);
        destroyPipeline(prepare); destroyPipeline(compose);
        if(descriptors) DestroyDescriptorPool(device,descriptors,nullptr);
        for(auto &im:images) {
            if(im.view) DestroyImageView(device,im.view,nullptr);
            if(im.image) DestroyImage(device,im.image,nullptr);
            if(im.memory) FreeMemory(device,im.memory,nullptr);
        }
        for(auto s:samplers) if(s) DestroySampler(device,s,nullptr);
        if(mapped) UnmapMemory(device,constantsMemory);
        if(constants) DestroyBuffer(device,constants,nullptr);
        if(constantsMemory) FreeMemory(device,constantsMemory,nullptr);
        if(instance) nrd::DestroyInstance(*instance);
    }
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memoryProperties.memoryTypeCount;i++)
            if((bits&(1u<<i)) && (memoryProperties.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("No suitable Vulkan memory type");
    }
    uint32_t image(VkFormat format,uint32_t divisor=1) {
        VkFormatProperties fp{}; getFormat(physical,format,&fp);
        auto required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        require((fp.optimalTilingFeatures&required)==required,"NRD texture format unsupported");
        images.emplace_back(); auto &im=images.back();
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D; ci.format=format;
        ci.extent={(width+divisor-1)/divisor,(height+divisor-1)/divisor,1};
        ci.mipLevels=ci.arrayLayers=1; ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
        check(CreateImage(device,&ci,nullptr,&im.image),"NRD image");
        VkMemoryRequirements mr{}; GetImageMemoryRequirements(device,im.image,&mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=mr.size; ai.memoryTypeIndex=memoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(AllocateMemory(device,&ai,nullptr,&im.memory),"NRD image memory");
        check(BindImageMemory(device,im.image,im.memory,0),"NRD bind image");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=im.image; vi.format=format; vi.viewType=VK_IMAGE_VIEW_TYPE_2D;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        check(CreateImageView(device,&vi,nullptr,&im.view),"NRD view");
        return uint32_t(images.size()-1);
    }
    static VkFormat format(nrd::Format f) {
        static const VkFormat formats[]={
            VK_FORMAT_R8_UNORM,VK_FORMAT_R8_SNORM,VK_FORMAT_R8_UINT,VK_FORMAT_R8_SINT,
            VK_FORMAT_R8G8_UNORM,VK_FORMAT_R8G8_SNORM,VK_FORMAT_R8G8_UINT,VK_FORMAT_R8G8_SINT,
            VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R8G8B8A8_SNORM,VK_FORMAT_R8G8B8A8_UINT,VK_FORMAT_R8G8B8A8_SINT,VK_FORMAT_R8G8B8A8_SRGB,
            VK_FORMAT_R16_UNORM,VK_FORMAT_R16_SNORM,VK_FORMAT_R16_UINT,VK_FORMAT_R16_SINT,VK_FORMAT_R16_SFLOAT,
            VK_FORMAT_R16G16_UNORM,VK_FORMAT_R16G16_SNORM,VK_FORMAT_R16G16_UINT,VK_FORMAT_R16G16_SINT,VK_FORMAT_R16G16_SFLOAT,
            VK_FORMAT_R16G16B16A16_UNORM,VK_FORMAT_R16G16B16A16_SNORM,VK_FORMAT_R16G16B16A16_UINT,VK_FORMAT_R16G16B16A16_SINT,VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R32_UINT,VK_FORMAT_R32_SINT,VK_FORMAT_R32_SFLOAT,
            VK_FORMAT_R32G32_UINT,VK_FORMAT_R32G32_SINT,VK_FORMAT_R32G32_SFLOAT,
            VK_FORMAT_R32G32B32_UINT,VK_FORMAT_R32G32B32_SINT,VK_FORMAT_R32G32B32_SFLOAT,
            VK_FORMAT_R32G32B32A32_UINT,VK_FORMAT_R32G32B32A32_SINT,VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_A2B10G10R10_UINT_PACK32,VK_FORMAT_B10G11R11_UFLOAT_PACK32,VK_FORMAT_E5B9G9R9_UFLOAT_PACK32};
        static_assert(sizeof(formats)/sizeof(*formats)==size_t(nrd::Format::MAX_NUM));
        require(uint32_t(f)<uint32_t(nrd::Format::MAX_NUM),"Unknown NRD format"); return formats[uint32_t(f)];
    }
    static VkDescriptorSetLayoutBinding binding(uint32_t b,VkDescriptorType type) {
        VkDescriptorSetLayoutBinding result{}; result.binding=b; result.descriptorType=type;
        result.descriptorCount=1; result.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT; return result;
    }
    void pipeline(Pipeline &p,const void *code,size_t size,const char *entry,
        const std::vector<std::vector<VkDescriptorSetLayoutBinding>> &bindings) {
        p.sets.resize(bindings.size());
        for(size_t i=0;i<bindings.size();i++) {
            VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            ci.bindingCount=uint32_t(bindings[i].size()); ci.pBindings=bindings[i].data();
            check(CreateDescriptorSetLayout(device,&ci,nullptr,&p.sets[i]),"NRD descriptor layout");
        }
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.setLayoutCount=uint32_t(p.sets.size()); li.pSetLayouts=p.sets.data();
        check(CreatePipelineLayout(device,&li,nullptr,&p.layout),"NRD pipeline layout");
        require(code && size,"NRD SPIR-V unavailable");
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize=size; si.pCode=static_cast<const uint32_t*>(code);
        VkShaderModule shader{}; check(CreateShaderModule(device,&si,nullptr,&shader),"NRD shader");
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.layout=p.layout; ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; ci.stage.module=shader; ci.stage.pName=entry;
        VkResult status=CreateComputePipelines(device,VK_NULL_HANDLE,1,&ci,nullptr,&p.pipeline);
        DestroyShaderModule(device,shader,nullptr); check(status,"NRD compute pipeline");
    }
    VkDescriptorBufferInfo upload(const void *data,uint32_t size) {
        require(slot<maxSlots && size<=stride,"NRD constants capacity exceeded");
        VkDescriptorBufferInfo info{constants,VkDeviceSize(slot++)*stride,size};
        if(data) std::memcpy(static_cast<char*>(mapped)+info.offset,data,size);
        else std::memset(static_cast<char*>(mapped)+info.offset,0,size);
        return info;
    }
    std::vector<VkDescriptorSet> allocate(const Pipeline &p) {
        std::vector<VkDescriptorSet> sets(p.sets.size());
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool=descriptors; ai.descriptorSetCount=uint32_t(sets.size()); ai.pSetLayouts=p.sets.data();
        check(AllocateDescriptorSets(device,&ai,sets.data()),"NRD descriptor allocation"); return sets;
    }
    void barrier(VkCommandBuffer cmd) {
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
    }
    void bind(VkCommandBuffer cmd,const Pipeline &p,const std::vector<VkDescriptorSet> &sets,uint32_t x,uint32_t y) {
        CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,uint32_t(sets.size()),sets.data(),0,nullptr);
        CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,p.pipeline); CmdDispatch(cmd,x,y,1);
    }
    void init(const vqe_denoiser_init_t &a) {
        require(a.abi==VQE_DENOISER_ABI && a.width && a.height && a.width<65536 && a.height<65536,"Denoiser ABI/size mismatch");
        device=a.device; physical=a.physical; width=a.width; height=a.height;
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(a.get_device_proc(device,"vk" #name)); require(name!=nullptr,"Missing vk" #name);
        DEVICE_FUNCTIONS(LOAD)
#undef LOAD
        auto memoryFn=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(a.get_instance_proc(a.instance,"vkGetPhysicalDeviceMemoryProperties"));
        auto propertiesFn=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(a.get_instance_proc(a.instance,"vkGetPhysicalDeviceProperties"));
        auto featuresFn=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(a.get_instance_proc(a.instance,"vkGetPhysicalDeviceFeatures"));
        getFormat=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(a.get_instance_proc(a.instance,"vkGetPhysicalDeviceFormatProperties"));
        require(memoryFn && propertiesFn && featuresFn && getFormat,"Missing Vulkan physical-device functions");
        memoryFn(physical,&memoryProperties); propertiesFn(physical,&properties);
        VkPhysicalDeviceFeatures features{}; featuresFn(physical,&features);
        require(features.shaderStorageImageReadWithoutFormat && features.shaderStorageImageWriteWithoutFormat,
            "NRD requires storage image reads/writes without format (engine must enable these core features)");
        require(properties.limits.maxPerStageDescriptorStorageImages>=10,"NRD needs ten storage images for signal conversion");
        require(properties.limits.maxPerStageDescriptorStorageBuffers>=47 && properties.limits.maxDescriptorSetStorageBuffers>=47 &&
            properties.limits.maxPerStageResources>=564,"NRD tracer metadata exceeds device descriptor limits");
        const auto &lib=*nrd::GetLibraryDesc();
        require(lib.normalEncoding==nrd::NormalEncoding::RGBA16_UNORM && lib.roughnessEncoding==nrd::RoughnessEncoding::LINEAR,"NRD encoding mismatch");
        nrd::DenoiserDesc dd{0,nrd::Denoiser::RELAX_DIFFUSE_SPECULAR};
        nrd::InstanceCreationDesc ci{}; ci.denoisers=&dd; ci.denoisersNum=1;
        require(nrd::CreateInstance(ci,instance)==nrd::Result::SUCCESS,"NRD instance creation failed");
        const auto &desc=*nrd::GetInstanceDesc(*instance);
        for(uint32_t i=0;i<EXTERNALS;i++) image(i==Z || i==CD || i==CS ? VK_FORMAT_R32_SFLOAT:VK_FORMAT_R16G16B16A16_SFLOAT);
        for(uint32_t i=0;i<desc.permanentPoolSize;i++) permanent.push_back(image(format(desc.permanentPool[i].format),desc.permanentPool[i].downsampleFactor));
        for(uint32_t i=0;i<desc.transientPoolSize;i++) transient.push_back(image(format(desc.transientPool[i].format),desc.transientPool[i].downsampleFactor));
        for(uint32_t i=0;i<2;i++) {
            VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            si.magFilter=si.minFilter=i ? VK_FILTER_LINEAR:VK_FILTER_NEAREST;
            si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            check(CreateSampler(device,&si,nullptr,&samplers[i]),"NRD sampler");
        }
        uint32_t align=uint32_t(std::max<VkDeviceSize>(256,properties.limits.minUniformBufferOffsetAlignment));
        stride=(std::max(desc.constantBufferMaxDataSize,272u)+align-1)/align*align;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size=VkDeviceSize(stride)*maxSlots; bi.usage=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        check(CreateBuffer(device,&bi,nullptr,&constants),"NRD constant buffer");
        VkMemoryRequirements mr{}; GetBufferMemoryRequirements(device,constants,&mr);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=mr.size;
        ai.memoryTypeIndex=memoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(AllocateMemory(device,&ai,nullptr,&constantsMemory),"NRD constants memory");
        check(BindBufferMemory(device,constants,constantsMemory,0),"NRD bind constants");
        check(MapMemory(device,constantsMemory,0,VK_WHOLE_SIZE,0,&mapped),"NRD map constants");
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,512},{VK_DESCRIPTOR_TYPE_SAMPLER,1024},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,8192},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,8192},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,64}};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=512;
        pool.poolSizeCount=5; pool.pPoolSizes=sizes;
        check(CreateDescriptorPool(device,&pool,nullptr,&descriptors),"NRD descriptor pool");
        pipelines.resize(desc.pipelinesNum);
        for(uint32_t i=0;i<desc.pipelinesNum;i++) {
            const auto &p=desc.pipelines[i];
            std::vector<std::vector<VkDescriptorSetLayoutBinding>> b(1+std::max(desc.resourcesSpaceIndex,desc.constantBufferAndSamplersSpaceIndex));
            auto &root=b[desc.constantBufferAndSamplersSpaceIndex];
            root.push_back(binding(lib.spirvBindingOffsets.constantBufferOffset+desc.constantBufferRegisterIndex,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
            for(uint32_t k=0;k<desc.samplersNum;k++) root.push_back(binding(lib.spirvBindingOffsets.samplerOffset+k,VK_DESCRIPTOR_TYPE_SAMPLER));
            for(uint32_t r=0;r<p.resourceRangesNum;r++) {
                bool storage=p.resourceRanges[r].descriptorType==nrd::DescriptorType::STORAGE_TEXTURE;
                uint32_t base=storage ? lib.spirvBindingOffsets.storageTextureAndBufferOffset:lib.spirvBindingOffsets.textureOffset;
                for(uint32_t k=0;k<p.resourceRanges[r].descriptorsNum;k++) b[desc.resourcesSpaceIndex].push_back(binding(base+k,storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE));
            }
            pipeline(pipelines[i],p.computeShaderSPIRV.bytecode,size_t(p.computeShaderSPIRV.size),desc.shaderEntryPoint,b);
        }
        std::vector<std::vector<VkDescriptorSetLayoutBinding>> b(1);
        for(uint32_t k=0;k<VQE_DENOISE_BUFFERS;k++) b[0].push_back(binding(k,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
        b[0].push_back(binding(VQE_DENOISE_BUFFERS,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER));
        for(uint32_t k=13;k<=22;k++) b[0].push_back(binding(k,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));
        pipeline(prepare,vqe_nrd_Prepare,sizeof(vqe_nrd_Prepare),"Prepare",b);
        pipeline(compose,vqe_nrd_Compose,sizeof(vqe_nrd_Compose),"Compose",b);
    }
    uint32_t resource(const nrd::ResourceDesc &r) {
        switch(r.type) {
            case nrd::ResourceType::IN_MV:return MV;
            case nrd::ResourceType::IN_NORMAL_ROUGHNESS:return N;
            case nrd::ResourceType::IN_VIEWZ:return Z;
            case nrd::ResourceType::IN_DIFF_CONFIDENCE:return CD;
            case nrd::ResourceType::IN_SPEC_CONFIDENCE:return CS;
            case nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST:return D;
            case nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST:return S;
            case nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST:return OD;
            case nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST:return OS;
            case nrd::ResourceType::PERMANENT_POOL:return permanent.at(r.indexInPool);
            case nrd::ResourceType::TRANSIENT_POOL:return transient.at(r.indexInPool);
            default:throw std::runtime_error("NRD requested an unbound signal");
        }
    }
    void record(const vqe_denoiser_frame_t &f) {
        // Host's render fence protects its single command buffer, descriptors
        // and scene data before this call. No hidden queue submits or waits.
        check(ResetDescriptorPool(device,descriptors,0),"NRD reset descriptors"); slot=0;
        nrd::CommonSettings common{};
        auto &pc=f.camera;
        // LH view space: screen right, screen down, forward. Vulkan image Y
        // matches the tracer's upProjection sign; no projection jitter here.
        for(int k=0;k<3;k++) {
            common.worldToViewMatrix[k*4]=pc[8+k];
            common.worldToViewMatrix[k*4+1]=pc[12+k];
            common.worldToViewMatrix[k*4+2]=pc[4+k];
            common.worldToViewMatrix[12]-=pc[8+k]*pc[k];
            common.worldToViewMatrix[13]-=pc[12+k]*pc[k];
            common.worldToViewMatrix[14]-=pc[4+k]*pc[k];
        }
        common.worldToViewMatrix[15]=1;
        common.viewToClipMatrix[0]=pc[11]; common.viewToClipMatrix[5]=pc[15];
        float nearPlane=std::max(pc[3],0.001f),farPlane=std::max(pc[21],nearPlane+1);
        common.viewToClipMatrix[10]=farPlane/(farPlane-nearPlane);
        common.viewToClipMatrix[11]=1; common.viewToClipMatrix[14]=-nearPlane*farPlane/(farPlane-nearPlane);
        std::memcpy(common.worldToViewMatrixPrev,previousValid ? previousView:common.worldToViewMatrix,sizeof(previousView));
        std::memcpy(common.viewToClipMatrixPrev,previousValid ? previousProjection:common.viewToClipMatrix,sizeof(previousProjection));
        for(int i=0;i<2;i++) {
            common.resourceSize[i]=common.resourceSizePrev[i]=common.rectSize[i]=common.rectSizePrev[i]=uint16_t(i ? height:width);
            // Tracer primary guide uses pixel centers plus this NDC offset.
            common.cameraJitter[i]=pc[22+i]*(i ? height:width)*0.5f;
            common.cameraJitterPrev[i]=f.previous[16+i]*(i ? height:width)*0.5f;
            common.cameraJitter[i]=std::clamp(common.cameraJitter[i],-0.5f,0.5f);
            common.cameraJitterPrev[i]=std::clamp(common.cameraJitterPrev[i],-0.5f,0.5f);
        }
        common.isMotionVectorInWorldSpace=true; common.motionVectorScale[2]=1;
        common.frameIndex=frameIndex++;
        common.denoisingRange=1000000;
        common.isHistoryConfidenceAvailable=true;
        common.accumulationMode=!transitioned ? nrd::AccumulationMode::CLEAR_AND_RESTART:
            (f.reset ? nrd::AccumulationMode::RESTART:nrd::AccumulationMode::CONTINUE);
        common.timeDeltaBetweenFrames=f.frame_ms;
        require(nrd::SetCommonSettings(*instance,common)==nrd::Result::SUCCESS,"NRD common settings");
        nrd::RelaxSettings settings{};
        settings.diffuseMaxAccumulatedFrameNum=settings.specularMaxAccumulatedFrameNum=std::clamp(f.history,1u,32u);
        settings.diffuseMaxFastAccumulatedFrameNum=settings.specularMaxFastAccumulatedFrameNum=std::max(1u,f.history/5);
        settings.enableAntiFirefly=true;
        settings.hitDistanceReconstructionMode=nrd::HitDistanceReconstructionMode::AREA_3X3;
        require(nrd::SetDenoiserSettings(*instance,0,&settings)==nrd::Result::SUCCESS,"NRD RELAX settings");
        const nrd::DispatchDesc *dispatches{}; uint32_t count{}; nrd::Identifier id=0;
        require(nrd::GetComputeDispatches(*instance,&id,1,dispatches,count)==nrd::Result::SUCCESS && count<maxSlots-2,"NRD dispatches");
        // Validate every binding before writing GPU commands; failure can safely
        // fall back to native reconstruction with no partially bound NRD work.
        for(uint32_t k=0;k<count;k++) for(uint32_t r=0;r<dispatches[k].resourcesNum;r++) (void)resource(dispatches[k].resources[r]);
        if(!transitioned) {
            std::vector<VkImageMemoryBarrier> barriers;
            for(auto &im:images) {
                VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
                b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
                b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
                b.image=im.image; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; barriers.push_back(b);
            }
            CmdPipelineBarrier(f.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,uint32_t(barriers.size()),barriers.data());
        }
        barrier(f.command);
        auto conversion=allocate(prepare);
        struct Params { float camera[32],previous[32]; uint32_t size[4]; } params{};
        std::memcpy(params.camera,f.camera,128); std::memcpy(params.previous,f.previous,128);
        params.size[0]=width; params.size[1]=height;
        VkDescriptorBufferInfo binfo[13]{};
        for(uint32_t k=0;k<12;k++) binfo[k]={f.buffers[k],0,VK_WHOLE_SIZE};
        binfo[12]=upload(&params,sizeof(params));
        VkDescriptorImageInfo iinfo[10]{};
        for(uint32_t k=0;k<9;k++) iinfo[k]={VK_NULL_HANDLE,images[k].view,VK_IMAGE_LAYOUT_GENERAL};
        iinfo[9]={VK_NULL_HANDLE,f.output,VK_IMAGE_LAYOUT_GENERAL};
        VkWriteDescriptorSet writes[23]{};
        for(uint32_t k=0;k<23;k++) {
            auto &w=writes[k]; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet=conversion[0]; w.dstBinding=k; w.descriptorCount=1;
            w.descriptorType=k<12 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:(k==12 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            if(k<=12) w.pBufferInfo=&binfo[k]; else w.pImageInfo=&iinfo[k-13];
        }
        UpdateDescriptorSets(device,23,writes,0,nullptr);
        bind(f.command,prepare,conversion,(width+7)/8,(height+7)/8); barrier(f.command);
        const auto &desc=*nrd::GetInstanceDesc(*instance); const auto &offset=nrd::GetLibraryDesc()->spirvBindingOffsets;
        for(uint32_t k=0;k<count;k++) {
            const auto &d=dispatches[k]; auto &p=pipelines.at(d.pipelineIndex);
            auto sets=allocate(p);
            VkDescriptorBufferInfo cb=upload(d.constantBufferData,d.constantBufferDataSize ? d.constantBufferDataSize:16);
            std::vector<VkDescriptorImageInfo> info(d.resourcesNum+2);
            std::vector<VkWriteDescriptorSet> ws(d.resourcesNum+3);
            auto &cw=ws[0]; cw.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            cw.dstSet=sets[desc.constantBufferAndSamplersSpaceIndex]; cw.dstBinding=offset.constantBufferOffset+desc.constantBufferRegisterIndex;
            cw.descriptorCount=1; cw.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; cw.pBufferInfo=&cb;
            for(uint32_t s=0;s<2;s++) {
                info[s]={samplers[s],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};
                auto &w=ws[s+1]; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w.dstSet=sets[desc.constantBufferAndSamplersSpaceIndex]; w.dstBinding=offset.samplerOffset+s;
                w.descriptorCount=1; w.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER; w.pImageInfo=&info[s];
            }
            uint32_t srv=0,uav=0;
            for(uint32_t r=0;r<d.resourcesNum;r++) {
                bool storage=d.resources[r].descriptorType==nrd::DescriptorType::STORAGE_TEXTURE;
                info[r+2]={VK_NULL_HANDLE,images[resource(d.resources[r])].view,VK_IMAGE_LAYOUT_GENERAL};
                auto &w=ws[r+3]; w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w.dstSet=sets[desc.resourcesSpaceIndex];
                w.dstBinding=storage ? offset.storageTextureAndBufferOffset+uav++:offset.textureOffset+srv++;
                w.descriptorCount=1; w.descriptorType=storage ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w.pImageInfo=&info[r+2];
            }
            UpdateDescriptorSets(device,uint32_t(ws.size()),ws.data(),0,nullptr);
            bind(f.command,p,sets,d.gridWidth,d.gridHeight); barrier(f.command);
        }
        bind(f.command,compose,conversion,(width+7)/8,(height+7)/8); barrier(f.command);
        transitioned=previousValid=true;
        std::memcpy(previousView,common.worldToViewMatrix,sizeof(previousView));
        std::memcpy(previousProjection,common.viewToClipMatrix,sizeof(previousProjection));
    }
};
void error(char *output,uint32_t size,const char *message) { if(output && size) std::snprintf(output,size,"%s",message); }
}
EXPORT void *vqe_denoiser_create(const vqe_denoiser_init_t *init,char *message,uint32_t size) {
    try { require(init!=nullptr,"Null initialization"); auto s=std::make_unique<State>(); s->init(*init); return s.release(); }
    catch(const std::exception &e) { error(message,size,e.what()); return nullptr; }
}
EXPORT int vqe_denoiser_record(void *state,const vqe_denoiser_frame_t *frame,char *message,uint32_t size) {
    try { require(state && frame,"Null denoiser/frame"); static_cast<State*>(state)->record(*frame); return 1; }
    catch(const std::exception &e) { error(message,size,e.what()); return 0; }
}
EXPORT void vqe_denoiser_destroy(void *state) { delete static_cast<State*>(state); }
