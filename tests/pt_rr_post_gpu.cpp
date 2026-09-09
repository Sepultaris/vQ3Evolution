// Headless, bounded-by-caller GPU equivalence check. No game/window/settings.
// Runs the frozen reference and production SPIR-V on the SAME HDR/guide inputs.
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>
#define CHECK(call) do { VkResult r=(call); if(r!=VK_SUCCESS) { std::fprintf(stderr,"%s: %d\n",#call,r); std::exit(1); } } while(0)
static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory;
struct Buffer { VkBuffer handle; VkDeviceMemory memory; void *mapped; };
struct Image { VkImage handle; VkImageView view; VkDeviceMemory memory; };
static uint32_t memoryType(uint32_t mask,VkMemoryPropertyFlags flags) {
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)
        if((mask&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
    std::fprintf(stderr,"No matching memory type\n"); std::exit(1);
}
static Buffer buffer(VkDeviceSize size) {
    Buffer b{};
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size=size; info.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    CHECK(vkCreateBuffer(device,&info,nullptr,&b.handle));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,b.handle,&req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(vkAllocateMemory(device,&alloc,nullptr,&b.memory));
    CHECK(vkBindBufferMemory(device,b.handle,b.memory,0));
    CHECK(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapped)); return b;
}
static Image image(uint32_t width,uint32_t height,VkFormat format) {
    Image i{};
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; info.imageType=VK_IMAGE_TYPE_2D;
    info.format=format; info.extent={width,height,1}; info.mipLevels=info.arrayLayers=1;
    info.samples=VK_SAMPLE_COUNT_1_BIT; info.tiling=VK_IMAGE_TILING_OPTIMAL;
    info.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    CHECK(vkCreateImage(device,&info,nullptr,&i.handle));
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device,i.handle,&req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; alloc.allocationSize=req.size;
    alloc.memoryTypeIndex=memoryType(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(vkAllocateMemory(device,&alloc,nullptr,&i.memory)); CHECK(vkBindImageMemory(device,i.handle,i.memory,0));
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=i.handle;
    view.viewType=VK_IMAGE_VIEW_TYPE_2D; view.format=format; view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    CHECK(vkCreateImageView(device,&view,nullptr,&i.view)); return i;
}
static void destroy(Buffer b) { vkUnmapMemory(device,b.memory); vkDestroyBuffer(device,b.handle,nullptr); vkFreeMemory(device,b.memory,nullptr); }
static void destroy(Image i) { vkDestroyImageView(device,i.view,nullptr); vkDestroyImage(device,i.handle,nullptr); vkFreeMemory(device,i.memory,nullptr); }
static VkPipeline pipeline(const char *path,VkPipelineLayout layout) {
    std::ifstream file(path,std::ios::binary|std::ios::ate); assert(file);
    std::vector<uint32_t> bytes(size_t(file.tellg())/4); file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()*4);
    VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; moduleInfo.codeSize=bytes.size()*4; moduleInfo.pCode=bytes.data();
    VkShaderModule module; CHECK(vkCreateShaderModule(device,&moduleInfo,nullptr,&module));
    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; info.layout=layout;
    info.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,module,"main",nullptr};
    VkPipeline p; CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&p)); vkDestroyShaderModule(device,module,nullptr); return p;
}
static void barrier(VkCommandBuffer cmd,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkAccessFlags read,VkAccessFlags write) {
    VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=read; b.dstAccessMask=write;
    vkCmdPipelineBarrier(cmd,src,dst,0,1,&b,0,nullptr,0,nullptr);
}
int main(int argc,char **argv) {
    assert(argc==3);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="VQ3E post equivalence"; app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; instanceInfo.pApplicationInfo=&app;
    VkInstance instance; CHECK(vkCreateInstance(&instanceInfo,nullptr,&instance));
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,nullptr)); assert(count);
    std::vector<VkPhysicalDevice> devices(count); CHECK(vkEnumeratePhysicalDevices(instance,&count,devices.data()));
    VkPhysicalDevice physical=devices[0];
    for(auto p:devices) { VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(p,&props); if(props.vendorID==0x10de) physical=p; }
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props); std::printf("DEVICE %s\n",props.deviceName);
    vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr); std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data()); uint32_t family=0;
    while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) ++family;
    assert(family<count);
    float priority=1; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex=family; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
    VkPhysicalDeviceFeatures features{}; features.shaderStorageImageExtendedFormats=VK_TRUE;
    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.queueCreateInfoCount=1; deviceInfo.pQueueCreateInfos=&queueInfo; deviceInfo.pEnabledFeatures=&features;
    CHECK(vkCreateDevice(physical,&deviceInfo,nullptr,&device)); VkQueue queue; vkGetDeviceQueue(device,family,0,&queue);
    VkDescriptorSetLayout layouts[2]; VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    CHECK(vkCreateDescriptorSetLayout(device,&layoutInfo,nullptr,&layouts[0]));
    VkDescriptorSetLayoutBinding bindings[]={{0,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {5,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{6,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
        {12,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
    layoutInfo.bindingCount=4; layoutInfo.pBindings=bindings; CHECK(vkCreateDescriptorSetLayout(device,&layoutInfo,nullptr,&layouts[1]));
    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT,0,128}; VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pi.setLayoutCount=2; pi.pSetLayouts=layouts; pi.pushConstantRangeCount=1; pi.pPushConstantRanges=&pushRange;
    VkPipelineLayout layout; CHECK(vkCreatePipelineLayout(device,&pi,nullptr,&layout));
    VkPipeline pipelines[]={pipeline(argv[1],layout),pipeline(argv[2],layout)};
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; poolInfo.queueFamilyIndex=family;
    VkCommandPool pool; CHECK(vkCreateCommandPool(device,&poolInfo,nullptr,&pool));
    VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ci.commandPool=pool; ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ci.commandBufferCount=1;
    VkCommandBuffer cmd; CHECK(vkAllocateCommandBuffers(device,&ci,&cmd));
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; CHECK(vkCreateFence(device,&fi,nullptr,&fence));
    unsigned cases=0; uint64_t differences=0; int maxDifference=0;
    for(auto extent: {VkExtent2D{1,1},VkExtent2D{17,13},VkExtent2D{1920,1080}}) {
        size_t pixels=size_t(extent.width)*extent.height;
        Buffer upload=buffer(pixels*8), budgets=buffer(pixels*16), readback=buffer(pixels*8);
        Image input=image(extent.width,extent.height,VK_FORMAT_R16G16B16A16_SFLOAT);
        Image outputs[]={image(extent.width,extent.height,VK_FORMAT_R8G8B8A8_UNORM),image(extent.width,extent.height,VK_FORMAT_R8G8B8A8_UNORM)};
        // Exercise finite HDR, negative values, subnormals, NaN and infinity.
        uint32_t rng=719;
        for(size_t i=0;i<pixels*4;++i) { rng=rng*1664525u+1013904223u; static_cast<uint16_t*>(upload.mapped)[i]=uint16_t(rng>>16); }
        std::memset(budgets.mapped,0,pixels*16);
        for(size_t i=0;i<pixels;++i) static_cast<float*>(budgets.mapped)[i*4+3]=(i%3)?2.f:4.f;
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,6},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}};
        VkDescriptorPoolCreateInfo di{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; di.maxSets=2; di.poolSizeCount=2; di.pPoolSizes=sizes;
        VkDescriptorPool descriptors; CHECK(vkCreateDescriptorPool(device,&di,nullptr,&descriptors));
        VkDescriptorSet sets[2]; VkDescriptorSetLayout setLayouts[]={layouts[1],layouts[1]};
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool=descriptors; ai.descriptorSetCount=2; ai.pSetLayouts=setLayouts;
        CHECK(vkAllocateDescriptorSets(device,&ai,sets));
        for(int i=0;i<2;++i) {
            VkDescriptorImageInfo images[]={{VK_NULL_HANDLE,input.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,input.view,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,outputs[i].view,VK_IMAGE_LAYOUT_GENERAL}};
            VkDescriptorBufferInfo bi{budgets.handle,0,VK_WHOLE_SIZE}; VkWriteDescriptorSet writes[4]{};
            for(int j=0;j<4;++j) { writes[j].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[j].dstSet=sets[i]; writes[j].dstBinding=bindings[j].binding;
                writes[j].descriptorCount=1; writes[j].descriptorType=bindings[j].descriptorType; if(j<3) writes[j].pImageInfo=&images[j]; else writes[j].pBufferInfo=&bi; }
            vkUpdateDescriptorSets(device,4,writes,0,nullptr);
        }
        bool initialized=false;
        for(float exposure: {.01f,1.f,11.313708f,16.f}) for(float strength: {0.f,.5f,1.f}) for(float debug: {0.f,1.f}) {
            CHECK(vkResetCommandBuffer(cmd,0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CHECK(vkBeginCommandBuffer(cmd,&begin));
            if(!initialized) {
                VkImageMemoryBarrier transitions[3]{}; VkImage handles[]={input.handle,outputs[0].handle,outputs[1].handle};
                for(int i=0;i<3;++i) { auto &b=transitions[i]; b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
                    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=handles[i]; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT; }
                vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,3,transitions);
                VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={extent.width,extent.height,1};
                vkCmdCopyBufferToImage(cmd,upload.handle,input.handle,VK_IMAGE_LAYOUT_GENERAL,1,&copy); initialized=true;
            }
            barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_HOST_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT);
            float push[32]{}; push[19]=exposure; push[20]=strength; push[21]=debug; push[28]=4;
            for(int i=0;i<2;++i) {
                vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelines[i]); vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,1,1,&sets[i],0,nullptr);
                vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,push); vkCmdDispatch(cmd,(extent.width+7)/8,(extent.height+7)/8,1);
            }
            barrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
            for(int i=0;i<2;++i) { VkBufferImageCopy copy{}; copy.bufferOffset=pixels*4*i; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; copy.imageExtent={extent.width,extent.height,1};
                vkCmdCopyImageToBuffer(cmd,outputs[i].handle,VK_IMAGE_LAYOUT_GENERAL,readback.handle,1,&copy); }
            barrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_HOST_READ_BIT);
            CHECK(vkEndCommandBuffer(cmd)); CHECK(vkResetFences(device,1,&fence)); VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&cmd;
            CHECK(vkQueueSubmit(queue,1,&submit,fence)); CHECK(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull));
            auto bytes=static_cast<uint8_t*>(readback.mapped);
            for(size_t i=0;i<pixels*4;++i) { int delta=std::abs(int(bytes[i])-int(bytes[pixels*4+i])); differences+=delta!=0; maxDifference=std::max(maxDifference,delta); }
            ++cases;
        }
        vkDestroyDescriptorPool(device,descriptors,nullptr); destroy(input); for(auto i:outputs) destroy(i); destroy(upload); destroy(budgets); destroy(readback);
    }
    std::printf("%u same-input GPU cases: %llu differing channels, maximum 8-bit difference %d\n",cases,(unsigned long long)differences,maxDifference);
    vkDestroyFence(device,fence,nullptr); vkDestroyCommandPool(device,pool,nullptr); for(auto p:pipelines) vkDestroyPipeline(device,p,nullptr);
    vkDestroyPipelineLayout(device,layout,nullptr); for(auto l:layouts) vkDestroyDescriptorSetLayout(device,l,nullptr);
    vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    return differences?1:0;
}
