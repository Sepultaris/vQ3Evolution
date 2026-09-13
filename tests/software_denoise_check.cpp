// Offscreen GPU checks: actual native temporal shader and actual NRD adapter.
// Synthetic two-sample inputs have a known reference. No RT extensions, game
// window, SDK stubs or frame generation. Not a game FPS benchmark.
#include "software_denoiser.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" void *vqe_denoiser_create(const vqe_denoiser_init_t *,char *,uint32_t);
extern "C" int vqe_denoiser_record(void *,const vqe_denoiser_frame_t *,char *,uint32_t);
extern "C" void vqe_denoiser_destroy(void *);
static void check(VkResult r) { if(r!=VK_SUCCESS) throw std::runtime_error("Vulkan fixture failure "+std::to_string(int(r))); }
static void expect(bool b,const char *m) { if(!b) throw std::runtime_error(m); }
struct Buffer { VkBuffer handle{}; VkDeviceMemory memory{}; float *data{}; };
struct GPU {
    static constexpr unsigned W=128,H=96,P=W*H;
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkCommandPool pool{}; VkCommandBuffer cmd{};
    VkImage image{}; VkDeviceMemory imageMemory{}; VkImageView view{};
    VkQueryPool timestamps{}; float tick=0;
    Buffer readback; std::vector<Buffer> buffers;
    uint32_t family{}; bool initialized=false;
    uint32_t type(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory.memoryTypeCount;i++) if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Missing host-coherent memory");
    }
    Buffer buffer(size_t bytes,VkBufferUsageFlags usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        Buffer b; VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size=bytes; ci.usage=usage;
        check(vkCreateBuffer(device,&ci,nullptr,&b.handle)); VkMemoryRequirements r{}; vkGetBufferMemoryRequirements(device,b.handle,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=r.size;
        ai.memoryTypeIndex=type(r.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device,&ai,nullptr,&b.memory)); check(vkBindBufferMemory(device,b.handle,b.memory,0));
        check(vkMapMemory(device,b.memory,0,bytes,0,reinterpret_cast<void**>(&b.data))); std::memset(b.data,0,bytes); return b;
    }
    void init(unsigned index) {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_2;
        VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ii.pApplicationInfo=&app; check(vkCreateInstance(&ii,nullptr,&instance));
        uint32_t count=0; check(vkEnumeratePhysicalDevices(instance,&count,nullptr)); std::vector<VkPhysicalDevice> list(count);
        check(vkEnumeratePhysicalDevices(instance,&count,list.data())); expect(index<count,"Device index unavailable"); physical=list[index];
        vkGetPhysicalDeviceMemoryProperties(physical,&memory); VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(physical,&props);
        tick=props.limits.timestampPeriod; std::printf("GPU: %s; zero device extensions (no hardware RT)\n",props.deviceName);
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr); std::vector<VkQueueFamilyProperties> qs(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,qs.data());
        for(family=0;family<count;family++) if((qs[family].queueFlags&VK_QUEUE_COMPUTE_BIT) && qs[family].timestampValidBits) break;
        expect(family<count,"No timestamp-capable compute queue");
        float priority=1; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
        VkPhysicalDeviceFeatures features{}; vkGetPhysicalDeviceFeatures(physical,&features);
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi; di.pEnabledFeatures=&features;
        check(vkCreateDevice(physical,&di,nullptr,&device)); vkGetDeviceQueue(device,family,0,&queue);
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex=family;
        check(vkCreateCommandPool(device,&cp,nullptr,&pool)); VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool=pool; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount=1; check(vkAllocateCommandBuffers(device,&ca,&cmd));
        VkImageCreateInfo im{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; im.imageType=VK_IMAGE_TYPE_2D; im.format=VK_FORMAT_R8G8B8A8_UNORM;
        im.extent={W,H,1}; im.mipLevels=im.arrayLayers=1; im.samples=VK_SAMPLE_COUNT_1_BIT; im.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        check(vkCreateImage(device,&im,nullptr,&image)); VkMemoryRequirements r{}; vkGetImageMemoryRequirements(device,image,&r);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=r.size; ai.memoryTypeIndex=type(r.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device,&ai,nullptr,&imageMemory)); check(vkBindImageMemory(device,image,imageMemory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=image; vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=im.format;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}; check(vkCreateImageView(device,&vi,nullptr,&view));
        readback=buffer(P*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        for(unsigned i=0;i<48;i++) buffers.push_back(buffer(P*48));
        VkQueryPoolCreateInfo query{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; query.queryType=VK_QUERY_TYPE_TIMESTAMP; query.queryCount=2;
        check(vkCreateQueryPool(device,&query,nullptr,&timestamps));
    }
    void begin() {
        check(vkResetCommandBuffer(cmd,0)); VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; check(vkBeginCommandBuffer(cmd,&bi));
        if(!initialized) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
            b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; b.image=image; b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
            b.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT; vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&b);
            initialized=true;
        }
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;
        b.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&b,0,nullptr,0,nullptr);
        vkCmdResetQueryPool(cmd,timestamps,0,2); vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,timestamps,0);
    }
    double end(bool copy=false) {
        vkCmdWriteTimestamp(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,timestamps,1);
        VkMemoryBarrier b{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; b.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&b,0,nullptr,0,nullptr);
        if(copy) {
            VkBufferImageCopy region{}; region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1}; region.imageExtent={W,H,1};
            vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_GENERAL,readback.handle,1,&region);
            b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&b,0,nullptr,0,nullptr);
        }
        check(vkEndCommandBuffer(cmd)); VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount=1; si.pCommandBuffers=&cmd;
        check(vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE)); check(vkQueueWaitIdle(queue)); uint64_t time[2]{};
        check(vkGetQueryPoolResults(device,timestamps,0,2,sizeof(time),time,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
        return double(time[1]-time[0])*tick/1000000;
    }
    void destroy(Buffer &b) { vkUnmapMemory(device,b.memory); vkDestroyBuffer(device,b.handle,nullptr); vkFreeMemory(device,b.memory,nullptr); }
    ~GPU() {
        if(!device) return;
        vkDeviceWaitIdle(device); for(auto &b:buffers) destroy(b); destroy(readback);
        vkDestroyQueryPool(device,timestamps,nullptr); vkDestroyImageView(device,view,nullptr); vkDestroyImage(device,image,nullptr); vkFreeMemory(device,imageMemory,nullptr);
        vkDestroyCommandPool(device,pool,nullptr); vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    }
};
static float bits(uint32_t value) { float f; std::memcpy(&f,&value,4); return f; }
static void v(Buffer &b,unsigned i,float x,float y,float z,float w) { float *p=b.data+i*4; p[0]=x;p[1]=y;p[2]=z;p[3]=w; }
static void native(GPU &g,const char *file) {
    const unsigned ids[]={11,12,13,14,17,18,19,20,21,22,23,26,28,29,32,33,36,37,38,39,40,41,42,43,44,45,46,47};
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.push_back({3,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    for(auto id:ids) bindings.push_back({id,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
    VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; sl.bindingCount=unsigned(bindings.size()); sl.pBindings=bindings.data();
    VkDescriptorSetLayout setLayout{}; check(vkCreateDescriptorSetLayout(g.device,&sl,nullptr,&setLayout));
    VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,32}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets=1; dp.poolSizeCount=2; dp.pPoolSizes=sizes;
    VkDescriptorPool pool{}; check(vkCreateDescriptorPool(g.device,&dp,nullptr,&pool));
    VkDescriptorSetAllocateInfo a{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; a.descriptorPool=pool; a.descriptorSetCount=1; a.pSetLayouts=&setLayout;
    VkDescriptorSet set{}; check(vkAllocateDescriptorSets(g.device,&a,&set));
    for(auto id:ids) {
        VkDescriptorBufferInfo bi{g.buffers[id].handle,0,VK_WHOLE_SIZE}; VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet=set; w.dstBinding=id; w.descriptorCount=1; w.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w.pBufferInfo=&bi; vkUpdateDescriptorSets(g.device,1,&w,0,nullptr);
    }
    VkDescriptorImageInfo im{VK_NULL_HANDLE,g.view,VK_IMAGE_LAYOUT_GENERAL}; VkWriteDescriptorSet iw{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    iw.dstSet=set; iw.dstBinding=3; iw.descriptorCount=1; iw.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; iw.pImageInfo=&im; vkUpdateDescriptorSets(g.device,1,&iw,0,nullptr);
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,128}; VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount=1; pl.pSetLayouts=&setLayout; pl.pushConstantRangeCount=1; pl.pPushConstantRanges=&push;
    VkPipelineLayout layout{}; check(vkCreatePipelineLayout(g.device,&pl,nullptr,&layout));
    FILE *f=std::fopen(file,"rb"); expect(f!=nullptr,"Missing native temporal SPIR-V"); std::fseek(f,0,SEEK_END); size_t size=std::ftell(f);std::rewind(f);
    std::vector<uint32_t> code(size/4); expect(std::fread(code.data(),1,size,f)==size,"Shader read failed"); std::fclose(f);
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize=size; sm.pCode=code.data();
    VkShaderModule shader{}; check(vkCreateShaderModule(g.device,&sm,nullptr,&shader));
    VkComputePipelineCreateInfo pc{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pc.layout=layout; pc.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pc.stage.module=shader; pc.stage.pName="main"; pc.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    VkPipeline pipeline{}; check(vkCreateComputePipelines(g.device,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline));
    for(unsigned phase=0;phase<4;phase++) {
        for(unsigned i=0;i<GPU::P;i++) {
            float x=((i%GPU::W+.5f)/GPU::W*2-1)*10,y=((i/GPU::W+.5f)/GPU::H*2-1)*10;
            uint32_t identity=1+((i%GPU::W>=GPU::W/2) ? 65536:0);
            v(g.buffers[11],i,1,1,1,1); v(g.buffers[12],i,x,y,10,10); v(g.buffers[19],i,x,y,10,10); v(g.buffers[22],i,x,y,10,1);
            for(unsigned id:{13u,20u,23u}) v(g.buffers[id],i,0,0,-1,bits(identity));
            if(phase==2) v(g.buffers[20],i,0,0,-1,bits(identity+1));
            v(g.buffers[14],i,1,1,1,1); v(g.buffers[18],i,1,1,1,8); v(g.buffers[39],i,1,1,0,0);
            v(g.buffers[41],i,phase==3 ? 10.f:0,0,0,0);
            for(unsigned id:{32u,33u}) v(g.buffers[id],i,0,0,-1,1);
        }
        float *rp=g.buffers[17].data; rp[6]=1; rp[8]=1; rp[11]=1; rp[13]=1; rp[15]=1; rp[18]=phase==0 ? 4.f:8.f;
        float constants[32]{}; constants[6]=1; constants[8]=1; constants[11]=1; constants[13]=1; constants[15]=1; constants[19]=1;
        g.begin(); vkCmdBindPipeline(g.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
        vkCmdBindDescriptorSets(g.cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr); vkCmdPushConstants(g.cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,constants);
        vkCmdDispatch(g.cmd,GPU::W/8,GPU::H/8,1); g.end();
        float left=g.buffers[21].data[(GPU::H/2*GPU::W+GPU::W/4)*4+3];
        float right=g.buffers[21].data[(GPU::H/2*GPU::W+GPU::W*3/4)*4+3];
        std::printf("Native phase %u: static history %.1f, moving-object history %.1f\n",phase,left,right);
        expect(std::abs(left-(phase==0 ? 4:phase==1 ? 8:1))<.01f,"Static history/rejection incorrect");
        expect(std::abs(right-(phase<2 ? 4:1))<.01f,"Object history/rejection incorrect");
    }
    vkDestroyPipeline(g.device,pipeline,nullptr); vkDestroyShaderModule(g.device,shader,nullptr); vkDestroyPipelineLayout(g.device,layout,nullptr);
    vkDestroyDescriptorPool(g.device,pool,nullptr); vkDestroyDescriptorSetLayout(g.device,setLayout,nullptr);
    for(auto &b:g.buffers) std::memset(b.data,0,GPU::P*48);
    std::puts("PASS: local history, object cap, disocclusion and changed-light rejection");
}
static float rand01(uint32_t &s) { s^=s<<13;s^=s>>17;s^=s<<5; return (s&0xffffff)/16777216.f; }
static float tone(float x) { return 255*std::pow(std::clamp(x*(2.51f*x+.03f)/(x*(2.43f*x+.59f)+.14f),0.f,1.f),1/2.2f); }
static void nrdCheck(GPU &g) {
    vqe_denoiser_init_t init{VQE_DENOISER_ABI,GPU::W,GPU::H,g.instance,g.physical,g.device,vkGetInstanceProcAddr,vkGetDeviceProcAddr};
    char error[512]{}; void *state=vqe_denoiser_create(&init,error,sizeof(error)); expect(state!=nullptr,error);
    double rawMse=0,filteredMse=0,stepError=0; size_t samples=0,stepSamples=0; std::vector<double> costs;
    for(unsigned frame=0;frame<48;frame++) {
        uint32_t rng=12345+frame*137; std::vector<float> reference(GPU::P*3),raw(GPU::P*3);
        bool changed=frame>=40;
        for(unsigned i=0;i<GPU::P;i++) {
            float x=((i%GPU::W+.5f)/GPU::W*2-1)*10,y=((i/GPU::W+.5f)/GPU::H*2-1)*10;
            float texture=((i%GPU::W)/8)%2 ? .35f:.7f;
            float base=(changed ? .10f:.035f)*texture;
            float noise=(rand01(rng)+rand01(rng))*1.5f-.5f; // fixed 2-sample input
            v(g.buffers[0],i,base*noise,base*noise*.8f,base*noise*.5f,1);
            v(g.buffers[1],i,.005f*noise,.005f*noise,.005f*noise,1);
            v(g.buffers[2],i,x,y,10,10); v(g.buffers[3],i,texture,texture*.8f,texture*.5f,.6f);
            v(g.buffers[4],i,0,0,-1,.6f); v(g.buffers[5],i,x,y,10,1);
            v(g.buffers[8],i,5,5,0,0);
            v(g.buffers[9],i,changed ? .10f:0,changed ? .035f:0,0,0);
            for(unsigned c=0;c<3;c++) {
                float rgb=base*(c==0 ? 1:c==1 ? .8f:.5f)+.005f;
                reference[i*3+c]=tone(rgb); raw[i*3+c]=tone(std::max(rgb*noise,0.f));
            }
        }
        vqe_denoiser_frame_t f{}; f.command=g.cmd; f.output=g.view; f.frame=frame; f.history=16; f.reset=frame==0; f.frame_ms=16;
        for(unsigned i=0;i<VQE_DENOISE_BUFFERS;i++) f.buffers[i]=g.buffers[i].handle;
        f.camera[3]=.1f; f.camera[6]=1; f.camera[8]=1; f.camera[11]=1; f.camera[13]=1; f.camera[15]=1; f.camera[19]=1; f.camera[21]=10000;
        g.begin(); expect(vqe_denoiser_record(state,&f,error,sizeof(error))!=0,error); double cost=g.end(true);
        if(frame>=16 && frame<40) costs.push_back(cost);
        auto bytes=reinterpret_cast<unsigned char*>(g.readback.data);
        for(unsigned y=8;y<GPU::H-8;y++) for(unsigned x=8;x<GPU::W-8;x++) for(unsigned c=0;c<3;c++) {
            unsigned i=y*GPU::W+x; double e=bytes[i*4+c]-reference[i*3+c];
            if(frame>=24 && frame<40) { filteredMse+=e*e; double r=raw[i*3+c]-reference[i*3+c]; rawMse+=r*r; samples++; }
            if(frame>=46) { stepError+=std::abs(e); stepSamples++; }
        }
    }
    vqe_denoiser_destroy(state); std::sort(costs.begin(),costs.end());
    std::printf("NRD fixture: raw MSE %.3f, filtered MSE %.3f; lighting-step MAE %.3f/255; GPU median %.3f ms at 128x96\n",
        rawMse/samples,filteredMse/samples,stepError/stepSamples,costs[costs.size()/2]);
    expect(filteredMse<rawMse*.5,"NRD did not halve synthetic two-sample error");
    expect(stepError/stepSamples<12,"NRD did not recover from a lighting change");
    std::puts("PASS: NRD low-sample noise reduction, textured surface preservation and lighting-step response");
}
int main(int argc,char **argv) {
    try { expect(argc==3,"Usage: software_denoise_check deviceIndex softwareTemporal.cspv"); GPU gpu; gpu.init(unsigned(std::atoi(argv[1]))); native(gpu,argv[2]); nrdCheck(gpu); }
    catch(const std::exception &e) { std::fprintf(stderr,"FAIL: %s\n",e.what());return 1; }
    return 0;
}
