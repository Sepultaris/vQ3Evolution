/* Execute production resumable software material queries on a device with NO device extensions
 * or optional features, compare against independent double-precision brute
 * force, and validate the production BVH builder's bounds and indirection. */
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define RT_MAX_TRIANGLES 256u
#include "../code/renderer_vulkan/rt_software_bvh.h"
#include "../code/renderer_vulkan/rt_software_order.h"

#define STATIC_TRIS 96u
#define DYNAMIC_TRIS 40u
#define TRI_COUNT (STATIC_TRIS + DYNAMIC_TRIS)
#define QUERY_COUNT 4096u
#define CHECK(call) do { VkResult r = (call); if (r != VK_SUCCESS) { \
    fprintf(stderr, "%s failed: %d\n", #call, r); exit(1); } } while (0)
typedef struct { float origin[4], direction[4], trees[4], result[4]; } query_t;
typedef struct { VkBuffer buffer; VkDeviceMemory memory; void *mapped; } buffer_t;
static VkDevice device;
static VkPhysicalDevice physical;
static float vertices[TRI_COUNT * 9];
static uint32_t indices[TRI_COUNT * 3], list[RT_MAX_TRIANGLES];
static rt_bvh_node_t nodes[RT_MAX_TRIANGLES * 2];
static query_t queries[QUERY_COUNT];
static uint32_t flags[TRI_COUNT], links[4+RT_MAX_TRIANGLES*2*RT_SW_ORDER_COUNT];
static uint32_t expected_primitive[QUERY_COUNT];
static float expected_bary[QUERY_COUNT][2];
static uint32_t rng = 19;
static float random_float(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0f; }

static void validate_orders(uint32_t root, uint32_t end)
{
    for (unsigned octant=0; octant<RT_SW_ORDER_COUNT; ++octant) {
        unsigned seen[RT_MAX_TRIANGLES*2]={0}, count=0, node=root;
        while(node<end) {
            assert(node>=root && ++seen[node]==1);
            ++count;
            uint32_t link=links[4+node*RT_SW_ORDER_COUNT+octant];
            assert((link&~RT_SW_RIGHT_FIRST)<=end);
            node=(nodes[node].right&RT_BVH_LEAF_FLAG) ? link&~RT_SW_RIGHT_FIRST :
                ((link&RT_SW_RIGHT_FIRST) ? nodes[node].right:nodes[node].left);
        }
        assert(node==end && count==end-root);
    }
}

static void validate_tree(uint32_t node, uint32_t nodeCount, uint32_t listBase,
    uint32_t triBase, uint32_t triCount, unsigned *seen, unsigned depth)
{
    assert(node < nodeCount && depth <= RT_BVH_MAX_DEPTH);
    rt_bvh_node_t *n = &nodes[node];
    for (int a = 0; a < 3; ++a) assert(isfinite(n->mn[a]) && isfinite(n->mx[a]) && n->mn[a] <= n->mx[a]);
    if (n->right & RT_BVH_LEAF_FLAG) {
        unsigned count = n->right & ~RT_BVH_LEAF_FLAG;
        assert(count > 0 && n->left + count <= triCount);
        for (unsigned i = 0; i < count; ++i) {
            unsigned tri = list[listBase + n->left + i];
            assert(tri >= triBase && tri < triBase + triCount);
            assert(++seen[tri] == 1);
            for (unsigned v = 0; v < 3; ++v) for (int a = 0; a < 3; ++a) {
                float p = vertices[indices[tri * 3 + v] * 3 + a];
                assert(p >= n->mn[a] && p <= n->mx[a]);
            }
        }
    } else {
        unsigned children[2] = {n->left, n->right};
        for (int i = 0; i < 2; ++i) {
            assert(children[i] > node && children[i] < nodeCount);
            for (int a = 0; a < 3; ++a) {
                assert(nodes[children[i]].mn[a] >= n->mn[a]);
                assert(nodes[children[i]].mx[a] <= n->mx[a]);
            }
            validate_tree(children[i], nodeCount, listBase, triBase, triCount, seen, depth + 1);
        }
    }
}

static double dot(const double *a, const double *b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static void cross(const double *a, const double *b, double *o) {
    o[0]=a[1]*b[2]-a[2]*b[1]; o[1]=a[2]*b[0]-a[0]*b[2]; o[2]=a[0]*b[1]-a[1]*b[0];
}
static float reference(const query_t *ray) {
    unsigned query=(unsigned)(ray-queries);
    double best = ray->origin[3]; int hit = 0;
    for (unsigned t=0; t<TRI_COUNT; ++t) {
        if (!(t<STATIC_TRIS ? ray->trees[3] : ray->trees[1])) continue;
        unsigned mask=(flags[t]&0x08000000u) ? 16u : ((flags[t]&0x20000000u) ? 8u:7u);
        if (!(mask&(unsigned)ray->trees[0]) || t%11==0) continue; // rejected alpha candidate
        double e1[3], e2[3], s[3], d[3], p[3], q[3];
        for (int a=0; a<3; ++a) {
            float v = vertices[indices[t*3]*3+a];
            e1[a]=vertices[indices[t*3+1]*3+a]-v; e2[a]=vertices[indices[t*3+2]*3+a]-v;
            s[a]=ray->origin[a]-v; d[a]=ray->direction[a];
        }
        cross(d,e2,p); double det=dot(e1,p);
        if (fabs(det)<1e-9) continue;
        double u=dot(s,p)/det; if(u<0 || u>1) continue;
        cross(s,e1,q); double v=dot(d,q)/det; if(v<0 || u+v>1) continue;
        double distance=dot(e2,q)/det;
        if(distance>=ray->direction[3] && distance<best) {
            best=distance;hit=1;expected_primitive[query]=t;
            expected_bary[query][0]=(float)u;expected_bary[query][1]=(float)v;
        }
    }
    return hit ? (float)best : -1.0f;
}

static buffer_t make_buffer(const void *data, size_t size) {
    buffer_t b={0};
    VkBufferCreateInfo ci={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=size,.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
    CHECK(vkCreateBuffer(device,&ci,NULL,&b.buffer));
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device,b.buffer,&req);
    VkPhysicalDeviceMemoryProperties props; vkGetPhysicalDeviceMemoryProperties(physical,&props);
    unsigned i;
    for(i=0;i<props.memoryTypeCount;++i) if((req.memoryTypeBits&(1u<<i)) &&
        (props.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) break;
    assert(i<props.memoryTypeCount);
    VkMemoryAllocateInfo ai={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=i};
    CHECK(vkAllocateMemory(device,&ai,NULL,&b.memory));
    CHECK(vkBindBufferMemory(device,b.buffer,b.memory,0));
    CHECK(vkMapMemory(device,b.memory,0,size,0,&b.mapped)); memcpy(b.mapped,data,size);
    return b;
}

int main(int argc, char **argv) {
    assert((argc==2 || argc==3) && sizeof(rt_bvh_node_t)==32 && sizeof(query_t)==64);
    for(unsigned t=0;t<TRI_COUNT;++t) {
        // Shuffled, well separated clusters create many empty SAH bins.
        float x=((float)((t*37)%TRI_COUNT)-TRI_COUNT/2.0f)*8.0f;
        float y=(float)(t%5)*9.0f, z=(float)(t%7)*11.0f;
        float tri[9]={x,y,z,x,y+3,z,x,y,z+3};
        if(t%3==0) {tri[3]+=1.25f;tri[6]-=0.75f;} // oblique planes
        if(t%17==0) {memcpy(tri+3,tri,3*sizeof(float));} // degenerate triangles
        // Aligned world occluder and weapon: a weapon query must see past the
        // world; a mixed world/weapon query must still return the nearer world.
        if(t==1 || t==STATIC_TRIS+5) {
            float x=t==1 ? 0.0f:10.0f;
            const float pair[9]={x,0,0,x,3,0,x,0,3};
            memcpy(tri,pair,sizeof(tri));
        }
        memcpy(vertices+t*9,tri,sizeof(tri));
        for(unsigned v=0;v<3;++v) indices[t*3+v]=t*3+v;
    }
    rt_bvh_scene_t scene={vertices,indices,0};
    rt_bvh_output_t out={nodes,0,0,list,0};
    assert(rt_bvh_build(&out,&scene,STATIC_TRIS));
    uint32_t staticNodes=out.cursor;
    assert(staticNodes>8); // catches the empty-bin collapse in the draft builder
    scene.global_first=STATIC_TRIS;
    out=(rt_bvh_output_t){nodes+staticNodes,0,staticNodes,list+STATIC_TRIS,0};
    assert(rt_bvh_build(&out,&scene,DYNAMIC_TRIS));
    uint32_t nodeCount=staticNodes+out.cursor;
    unsigned seen[TRI_COUNT]={0}, reordered=0;
    validate_tree(0,staticNodes,0,0,STATIC_TRIS,seen,0);
    validate_tree(staticNodes,nodeCount,STATIC_TRIS,STATIC_TRIS,DYNAMIC_TRIS,seen,0);
    for(unsigned t=0;t<TRI_COUNT;++t) {assert(seen[t]==1);reordered+=(list[t]!=t);}
    assert(reordered>TRI_COUNT/2);
    assert(!rt_bvh_build(&out,&scene,0));
    assert(!rt_bvh_build(&out,&scene,RT_MAX_TRIANGLES));
    links[0]=staticNodes;links[1]=nodeCount;links[2]=STATIC_TRIS;
    rt_sw_order_tree(nodes,0,staticNodes,links+4);
    rt_sw_order_tree(nodes,staticNodes,nodeCount,links+4);
    validate_orders(0,staticNodes);
    validate_orders(staticNodes,nodeCount);
    for(unsigned t=0;t<TRI_COUNT;++t) flags[t]=t%5==0 ? 0x08000000u : (t%5==1 ? 0x20000000u:0u);
    for(unsigned i=0;i<QUERY_COUNT;++i) {
        query_t *q=&queries[i]; unsigned tri=i%TRI_COUNT;
        float *v=vertices+tri*9;
        q->origin[0]=v[0]-5; q->origin[1]=v[1]+0.7f; q->origin[2]=v[2]+0.7f;
        q->origin[3]=i%5 ? 2000.0f : 2.0f;
        q->direction[0]=1;
        if(i%7==0) q->direction[0]=-1;
        if(i%7==1) {q->direction[0]=0;q->direction[1]=1;} // parallel/outside
        if(i%7==2) {q->origin[0]=v[0];q->direction[0]=0;q->direction[2]=1;} // on slab
        if(i%7==3) {q->direction[1]=random_float()-0.5f;q->direction[2]=random_float()-0.5f;}
        const unsigned masks[8]={1,2,4,8,16,9,31,0};
        q->trees[0]=(float)masks[i%8]; q->direction[3]=i%13==0 ? 6.0f:0.000001f;
        q->trees[1]=1; q->trees[3]=1; // empty/static/dynamic/both
        if(i%37==0) { q->direction[0]=-0.0f; q->direction[1]=1; }
        if(i%37==1) { q->direction[0]=1; q->direction[1]=.01f; q->direction[2]=1e-32f; }
        if(i%37==2) {
            // All-nonzero direction through an interior point, with a tight
            // accepted distance interval: exercises the reciprocal box path.
            const float direction[3]={.7f,.2f,.1f};
            for(unsigned axis=0;axis<3;++axis) {
                q->direction[axis]=direction[axis];
                q->origin[axis]=(v[axis]+v[axis+3]+v[axis+6])/3-5*direction[axis];
            }
            q->direction[3]=4.9999f; q->origin[3]=5.0001f;
        }
        if(i%8==3) {
            // Hit-directed rays cover all eight ordering octants, including
            // candidates rejected before a farther accepted intersection.
            unsigned octant=(i/8)%8;
            const float direction[3]={.7f,.2f,.1f};
            for(unsigned axis=0;axis<3;++axis) {
                q->direction[axis]=(octant&(1u<<axis)) ? -direction[axis]:direction[axis];
                q->origin[axis]=(v[axis]+v[axis+3]+v[axis+6])/3-5*q->direction[axis];
            }
        }
        if(i>=QUERY_COUNT-16) {
            q->origin[0]=-5; q->origin[1]=q->origin[2]=.7f;
            q->origin[3]=i>=QUERY_COUNT-8 ? 12:20;
            q->direction[0]=1; q->direction[1]=q->direction[2]=0;
            q->direction[3]=.001f;
        }
        q->result[2]=reference(q);
    }
    VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,.pApplicationName="VQ3E software traversal test",.apiVersion=VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,.pApplicationInfo=&app};
    VkInstance instance; CHECK(vkCreateInstance(&ici,NULL,&instance));
    uint32_t count=0; CHECK(vkEnumeratePhysicalDevices(instance,&count,NULL));
    VkPhysicalDevice *devices=calloc(count,sizeof(*devices)); assert(devices && count);
    CHECK(vkEnumeratePhysicalDevices(instance,&count,devices));
    unsigned selected=argc==3 ? (unsigned)atoi(argv[2]) : 0; assert(selected<count);
    physical=devices[selected];free(devices);
    VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(physical,&props);
    printf("Device: %s; device extensions=0, optional features=0\n",props.deviceName);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,NULL);
    VkQueueFamilyProperties *families=calloc(count,sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families);
    unsigned family=0; while(family<count && !(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)) ++family;
    assert(family<count);free(families);
    float priority=1;
    VkDeviceQueueCreateInfo qi={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    VkDeviceCreateInfo dci={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&qi};
    CHECK(vkCreateDevice(physical,&dci,NULL,&device));
    VkQueue queue;vkGetDeviceQueue(device,family,0,&queue);
    buffer_t buffers[7]={make_buffer(nodes,sizeof(nodes)),make_buffer(vertices,sizeof(vertices)),
        make_buffer(indices,sizeof(indices)),make_buffer(list,sizeof(list)),make_buffer(queries,sizeof(queries)),
        make_buffer(flags,sizeof(flags)),make_buffer(links,sizeof(links))};
    unsigned binding_ids[7]={0,4,5,51,52,7,50};
    VkDescriptorSetLayoutBinding bindings[7]={0};
    for(unsigned i=0;i<7;++i) bindings[i]=(VkDescriptorSetLayoutBinding){binding_ids[i],VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,NULL};
    VkDescriptorSetLayoutCreateInfo slci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=7,.pBindings=bindings};
    VkDescriptorSetLayout sl;CHECK(vkCreateDescriptorSetLayout(device,&slci,NULL,&sl));
    VkDescriptorPoolSize ps={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7};
    VkDescriptorPoolCreateInfo pci={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=1,.poolSizeCount=1,.pPoolSizes=&ps};
    VkDescriptorPool pool;CHECK(vkCreateDescriptorPool(device,&pci,NULL,&pool));
    VkDescriptorSetAllocateInfo sai={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=pool,.descriptorSetCount=1,.pSetLayouts=&sl};
    VkDescriptorSet set;CHECK(vkAllocateDescriptorSets(device,&sai,&set));
    for(unsigned i=0;i<7;++i) {
        VkDescriptorBufferInfo bi={buffers[i].buffer,0,VK_WHOLE_SIZE};
        VkWriteDescriptorSet w={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=set,.dstBinding=binding_ids[i],.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,.pBufferInfo=&bi};
        vkUpdateDescriptorSets(device,1,&w,0,NULL);
    }
    VkPushConstantRange range={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    VkPipelineLayoutCreateInfo plci={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&sl,
        .pushConstantRangeCount=1,.pPushConstantRanges=&range};
    VkPipelineLayout layout;CHECK(vkCreatePipelineLayout(device,&plci,NULL,&layout));
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
    uint32_t *code=malloc(size);assert(code && fread(code,1,size,f)==(size_t)size);fclose(f);
    VkShaderModuleCreateInfo sci={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=(size_t)size,.pCode=code};
    VkShaderModule shader;CHECK(vkCreateShaderModule(device,&sci,NULL,&shader));free(code);
    VkComputePipelineCreateInfo cpci={.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,.layout=layout,
        .stage={.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_COMPUTE_BIT,.module=shader,.pName="main"}};
    VkPipeline pipeline;CHECK(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cpci,NULL,&pipeline));
    VkCommandPoolCreateInfo cpi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    VkCommandPool cp;CHECK(vkCreateCommandPool(device,&cpi,NULL,&cp));
    VkCommandBufferAllocateInfo cai={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=cp,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer cmd;CHECK(vkAllocateCommandBuffers(device,&cai,&cmd));
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};CHECK(vkBeginCommandBuffer(cmd,&begin));
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,NULL);
    uint32_t queriesCount=QUERY_COUNT;vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&queriesCount);
    vkCmdDispatch(cmd,(queriesCount+63)/64,1,1);
    VkMemoryBarrier mb={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&mb,0,NULL,0,NULL);
    CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&cmd};
    unsigned hits=0,weapon_hits=0,skipped_static=0;
    for (unsigned phase=0;phase<8;++phase) {
    // Both trees, static only, dynamic only and empty, using the production
    // escape traversal and nonzero triangle-list offsets without shader stubs.
    // First retain the generic mixed-mask static tree. Then exercise the
    // production invariant (static world has mask 7; weapons are dynamic).
    uint32_t staticMask=0,trees=phase%4;
    for(unsigned t=0;t<TRI_COUNT;++t) {
        flags[t]=phase>=4 && t<STATIC_TRIS ? 0u:
            (t%5==0 ? 0x08000000u : (t%5==1 ? 0x20000000u:0u));
        if(t<STATIC_TRIS) staticMask|=(flags[t]&0x08000000u) ? 16u:
            ((flags[t]&0x20000000u) ? 8u:7u);
    }
    memcpy(buffers[5].mapped,flags,sizeof(flags));
    uint32_t *header=buffers[6].mapped;
    header[1]=trees==3 ? 0 : (trees==1 ? staticNodes:nodeCount);
    header[3]=trees==0 || trees==1 ? staticMask:0;
    for(unsigned i=0;i<QUERY_COUNT;++i) {
        queries[i].trees[1]=trees==0 || trees==2;
        queries[i].trees[3]=trees==0 || trees==1;
        queries[i].result[2]=reference(&queries[i]);
    }
    memcpy(buffers[4].mapped,queries,sizeof(queries));
    CHECK(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE));CHECK(vkQueueWaitIdle(queue));
    query_t *result=buffers[4].mapped;
    for(unsigned i=0;i<QUERY_COUNT;++i) {
        float expected=queries[i].result[2], actual=result[i].result[0];
        unsigned root=((unsigned)queries[i].trees[0]&header[3]) ? 0:staticNodes;
        if((unsigned)result[i].trees[2]!=root) {
            fprintf(stderr,"query %u started at wrong tree root\n",i);return 1;
        }
        skipped_static+=root==staticNodes && trees==0;
        if((expected>=0)!=(actual>=0) || (expected>=0 && fabsf(actual-expected)>0.002f) ||
            (expected>=0)!=(result[i].result[1]>=0)) {
            fprintf(stderr,"query %u expected %g, nearest %g, any %g\n",i,expected,actual,result[i].result[1]);return 1;
        }
        if(expected>=0 && ((unsigned)result[i].result[3]!=expected_primitive[i] ||
            fabsf(result[i].trees[0]-expected_bary[i][0])>0.0001f ||
            fabsf(result[i].trees[1]-expected_bary[i][1])>0.0001f)) {
            fprintf(stderr,"query %u primitive/barycentric mismatch\\n",i);return 1;
        }
        hits+=expected>=0;
        weapon_hits+=phase==4 && queries[i].trees[0]==8 && expected>=0;
    }
    }
    assert(hits>100 && weapon_hits>0 && skipped_static>0);
    for(unsigned i=0;i<7;++i) {vkUnmapMemory(device,buffers[i].memory);vkDestroyBuffer(device,buffers[i].buffer,NULL);vkFreeMemory(device,buffers[i].memory,NULL);}
    vkDestroyCommandPool(device,cp,NULL);vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);
    vkDestroyPipelineLayout(device,layout,NULL);vkDestroyDescriptorPool(device,pool,NULL);vkDestroyDescriptorSetLayout(device,sl,NULL);
    vkDestroyDevice(device,NULL);vkDestroyInstance(instance,NULL);
    printf("PASS: eight complete traversal orders; %u BVH nodes, %u reordered triangles, %u GPU rays (%u hits); %u excluded static-tree roots, %u weapon hits; masks, rejected candidates, tMin/tMax, primitive IDs, barycentrics and nearest/any-hit agree with brute force\n",nodeCount,reordered,QUERY_COUNT*8,hits,skipped_static,weapon_hits);
    return 0;
}
