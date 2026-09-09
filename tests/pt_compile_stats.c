/* Compile-only driver diagnostic. No window, swapchain, resource allocations,
 * command pools, command buffers, queue submissions or rendered workload.
 * The descriptor layout and feature subset match the native PT integrator.
 * Usage: pt_compile_stats.exe [--packed-emitters] [--rows=8|64|128] shader.cspv [more shader.cspv ...] */
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

static void checked(VkResult result, const char *what) {
    if (result != VK_SUCCESS) { fprintf(stderr, "%s failed: %d\n", what, result); exit(1); }
}
#define CHECK(expr) checked((expr), #expr)
static void print_message(int level, const char *format, ...) {
    (void)level;
    va_list args; va_start(args, format); vprintf(format, args); va_end(args);
}
static struct { VkDevice device; } vk;
static struct { void *(*Malloc)(size_t); void (*Free)(void *); void (*Printf)(int, const char *, ...); }
    ri = { malloc, free, print_message };
static struct { int integer; } stats_cvar = { 1 }, *r_pathTracingPipelineStats = &stats_cvar;
static int vk_rt_pipeline_statistics_supported(void) { return 1; }
#define qvkGetDeviceProcAddr vkGetDeviceProcAddr
#define PRINT_ALL 0
#define PRINT_WARNING 1
#include "../code/renderer_vulkan/pt_pipeline_stats.h"

int main(int argc, char **argv) {
    int firstFile=1;
    VkBool32 packed=VK_FALSE;
    int staged=0;
    if(argc>1 && !strcmp(argv[1],"--staged")) { staged=1; ++firstFile; }
    if(argc>firstFile && !strcmp(argv[firstFile],"--packed-emitters")) { packed=VK_TRUE; ++firstFile; }
    uint32_t rows=8;
    if(argc>firstFile && !strcmp(argv[firstFile],"--rows=64")) { rows=64; ++firstFile; }
    else if(argc>firstFile && !strcmp(argv[firstFile],"--rows=128")) { rows=128; ++firstFile; }
    else if(argc>firstFile && !strcmp(argv[firstFile],"--rows=8")) ++firstFile;
    if (argc <= firstFile) { fprintf(stderr, "Pass one or more compiled .cspv shaders\n"); return 2; }
    printf("SPECIALIZATION packed_emitters=%u rows=%u\n",packed,rows);
    VkApplicationInfo app = { .sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="VQ3 Evolution compile-only diagnostic", .apiVersion=VK_API_VERSION_1_2 };
    VkInstanceCreateInfo instance_info = { .sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo=&app };
    /* Opt-in validation is controlled by the Vulkan loader, e.g. VK_INSTANCE_LAYERS. */
    VkInstance instance;
    CHECK(vkCreateInstance(&instance_info, NULL, &instance));
    uint32_t count=0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, NULL));
    if (!count || count>32) return 1;
    VkPhysicalDevice devices[32], physical=VK_NULL_HANDLE;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices));
    for (uint32_t i=0; i<count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.vendorID==0x10de) { physical=devices[i]; printf("DEVICE %s\n", properties.deviceName); break; }
    }
    if (!physical) { fprintf(stderr, "No NVIDIA device; diagnostic not run\n"); return 1; }
    VkPhysicalDeviceDescriptorIndexingFeatures indexing = { .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .shaderSampledImageArrayNonUniformIndexing=VK_TRUE };
    VkPhysicalDeviceRayQueryFeaturesKHR ray = { .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
        .pNext=&indexing, .rayQuery=VK_TRUE };
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accel = { .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
        .pNext=&ray, .accelerationStructure=VK_TRUE };
    VkPhysicalDeviceBufferDeviceAddressFeatures address = { .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
        .pNext=&accel, .bufferDeviceAddress=VK_TRUE };
    VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR stats = { .sType=VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR,
        .pNext=&address, .pipelineExecutableInfo=VK_TRUE };
    const char *extensions[] = { VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        VK_KHR_RAY_QUERY_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
        VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME };
    uint32_t families=0, family=UINT32_MAX;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, NULL);
    if (!families || families>64) return 1;
    VkQueueFamilyProperties queues[64];
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &families, queues);
    for (uint32_t i=0; i<families; ++i) if (queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT) { family=i; break; }
    if (family==UINT32_MAX) return 1;
    float priority=1;
    VkDeviceQueueCreateInfo queue_info = { .sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=family, .queueCount=1, .pQueuePriorities=&priority };
    VkDeviceCreateInfo device_info = { .sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext=&stats,
        .queueCreateInfoCount=1, .pQueueCreateInfos=&queue_info,
        .enabledExtensionCount=4, .ppEnabledExtensionNames=extensions };
    CHECK(vkCreateDevice(physical, &device_info, NULL, &vk.device));
    VkDescriptorSetLayoutBinding bindings[48] = {0};
    for (uint32_t i=0; i<48; ++i) {
        bindings[i].binding=i; bindings[i].descriptorCount=i==9 ? 512:1;
        bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
        bindings[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    bindings[0].descriptorType=VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[1].descriptorType=bindings[2].descriptorType=bindings[9].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorType=bindings[24].descriptorType=bindings[25].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    VkDescriptorSetLayoutCreateInfo set_info = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount=48, .pBindings=bindings };
    VkDescriptorSetLayout set;
    CHECK(vkCreateDescriptorSetLayout(vk.device, &set_info, NULL, &set));
    VkDescriptorSetLayout sets[2]={set,VK_NULL_HANDLE};
    if(staged) {
        VkDescriptorSetLayoutBinding state_bindings[2] = {
            {0,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL},
            {1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL} };
        VkDescriptorSetLayoutCreateInfo state_info = { .sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount=2, .pBindings=state_bindings };
        CHECK(vkCreateDescriptorSetLayout(vk.device,&state_info,NULL,&sets[1]));
    }
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo layout_info = { .sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount=staged ? 2:1, .pSetLayouts=sets, .pushConstantRangeCount=1, .pPushConstantRanges=&push };
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(vk.device, &layout_info, NULL, &layout));
    for (int file=firstFile; file<argc; ++file) {
        FILE *input=fopen(argv[file], "rb");
        if (!input) { perror(argv[file]); return 1; }
        if (fseek(input,0,SEEK_END)) return 1;
        long length=ftell(input);
        if (length<20 || length%4 || length>16*1024*1024 || fseek(input,0,SEEK_SET)) return 1;
        uint32_t *code=malloc((size_t)length);
        if (!code || fread(code,1,(size_t)length,input)!=(size_t)length) return 1;
        fclose(input);
        if (code[0]!=0x07230203) return 1;
        VkShaderModuleCreateInfo module_info = { .sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize=(size_t)length, .pCode=code };
        VkShaderModule module;
        CHECK(vkCreateShaderModule(vk.device, &module_info, NULL, &module));
        uint32_t options[4]={VK_FALSE,VK_FALSE,packed,rows};
        VkSpecializationMapEntry entries[4]={{0,0,4},{1,4,4},{2,8,4},{3,12,4}};
        VkSpecializationInfo specialization={4,entries,sizeof(options),options};
        VkComputePipelineCreateInfo info = { .sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .flags=pipeline_statistics_flags(), .layout=layout,
            .stage={ .sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage=VK_SHADER_STAGE_COMPUTE_BIT,
                .module=module, .pName="main", .pSpecializationInfo=&specialization } };
        VkPipeline pipeline;
        printf("SHADER %s\n", argv[file]); fflush(stdout);
        clock_t started=clock();
        CHECK(vkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &info, NULL, &pipeline));
        printf("COMPILE_MS %.3f\n", 1000.0*(clock()-started)/CLOCKS_PER_SEC);
        pipeline_statistics_print(pipeline,(uint32_t)file,info.flags);
        vkDestroyPipeline(vk.device,pipeline,NULL);
        vkDestroyShaderModule(vk.device,module,NULL);
        free(code);
    }
    vkDestroyPipelineLayout(vk.device,layout,NULL);
    vkDestroyDescriptorSetLayout(vk.device,set,NULL);
    if(sets[1]) vkDestroyDescriptorSetLayout(vk.device,sets[1],NULL);
    vkDestroyDevice(vk.device,NULL);
    vkDestroyInstance(instance,NULL);
    return 0;
}
