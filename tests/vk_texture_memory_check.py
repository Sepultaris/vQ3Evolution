"""Exercise production Vulkan texture allocation with HD-sized mocked resources."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'code/renderer_vulkan/vk_image.c').read_text()
# Compile the actual allocation functions, not a parallel implementation.
body = source[source.index('struct StagingBuffer_t'):source.index('void gpuMemUsageInfo_f')]
body += source[source.index('uint32_t find_memory_type'):source.index('static void vk_stagBufferToDeviceLocalMem')]
body += source[source.index('static void vk_createImageAndBindWithMemory'):source.index('static void vk_createImageViewAndDescriptorSet')]
fixture = r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define IMAGE_CHUNK_SIZE (64u * 1024u * 1024u)
#define ERR_FATAL 1
#define PRINT_ALL 0
#define VK_CHECK(call) assert((call)==VK_SUCCESS)
#define ID(h) ((unsigned)(uintptr_t)(h))
#define HANDLE(t,n) ((t)(uintptr_t)(n))
typedef struct { uint32_t uploadWidth, uploadHeight, mipLevels; VkImage handle; } image_t;
static struct { VkDevice device; VkQueue queue; VkPhysicalDeviceMemoryProperties devMemProperties; } vk;
static void print_log(int level,const char *fmt,...) { (void)level; (void)fmt; }
static void error_log(int level,const char *fmt,...) { (void)level; (void)fmt; abort(); }
static struct { void (*Printf)(int,const char *,...); void (*Error)(int,const char *,...); } ri={print_log,error_log};
static void gpuMemUsageInfo_f(void) {}
static unsigned next_id, allocations, frees, waits, buffer_destroys;
static VkDeviceSize resource_size[1024], memory_size[1024];
static uint32_t memory_type[1024];
static VkMemoryRequirements image_req={22369620,256,1};
static VkResult qvkCreateBuffer(VkDevice d,const VkBufferCreateInfo *c,const void *a,VkBuffer *out) {
    (void)d; (void)a; *out=HANDLE(VkBuffer,++next_id); resource_size[next_id]=c->size; return VK_SUCCESS;
}
static void qvkGetBufferMemoryRequirements(VkDevice d,VkBuffer b,VkMemoryRequirements *r) {
    (void)d; *r=(VkMemoryRequirements){resource_size[ID(b)],256,1};
}
static VkResult qvkAllocateMemory(VkDevice d,const VkMemoryAllocateInfo *a,const void *c,VkDeviceMemory *out) {
    (void)d; (void)c; *out=HANDLE(VkDeviceMemory,++next_id);
    memory_size[next_id]=a->allocationSize; memory_type[next_id]=a->memoryTypeIndex; ++allocations; return VK_SUCCESS;
}
static VkResult qvkBindBufferMemory(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o) {
    (void)d; assert(o+resource_size[ID(b)]<=memory_size[ID(m)]); return VK_SUCCESS;
}
static void qvkDestroyBuffer(VkDevice d,VkBuffer b,const void *a) {
    (void)d; (void)a; assert(resource_size[ID(b)]); resource_size[ID(b)]=0; ++buffer_destroys;
}
static void qvkFreeMemory(VkDevice d,VkDeviceMemory m,const void *a) {
    (void)d; (void)a; assert(memory_size[ID(m)]); memory_size[ID(m)]=0; ++frees;
}
static VkResult qvkQueueWaitIdle(VkQueue q) { (void)q; ++waits; return VK_SUCCESS; }
static VkResult qvkCreateImage(VkDevice d,const VkImageCreateInfo *c,const void *a,VkImage *out) {
    (void)d; (void)c; (void)a; *out=HANDLE(VkImage,++next_id); resource_size[next_id]=image_req.size; return VK_SUCCESS;
}
static void qvkGetImageMemoryRequirements(VkDevice d,VkImage i,VkMemoryRequirements *out) {
    (void)d; (void)i; *out=image_req;
}
static VkResult qvkBindImageMemory(VkDevice d,VkImage i,VkDeviceMemory m,VkDeviceSize o) {
    (void)d;
    assert(o+resource_size[ID(i)]<=memory_size[ID(m)]);
    assert((o % image_req.alignment)==0);
    assert(image_req.memoryTypeBits & (1u<<memory_type[ID(m)]));
    return VK_SUCCESS;
}
#include "production.h"
int main(void) {
    vk.devMemProperties.memoryTypeCount=2;
    vk.devMemProperties.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vk.devMemProperties.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    vk_createStagingBuffer(8u<<20);
    VkBuffer first=StagBuf.buff;
    vk_ensure_staging_buffer(4u<<20);
    assert(StagBuf.buff==first && !waits);
    vk_ensure_staging_buffer(16u<<20); // Original 2048x2048 upload failure.
    assert(StagBuf.capacity==(16u<<20) && waits==1 && buffer_destroys==1);
    vk_ensure_staging_buffer(22369620); // Entire 2048x2048 RGBA mip chain.
    assert(StagBuf.capacity==22369620 && waits==2);
    vk_ensure_staging_buffer(4);
    assert(waits==2);
    for (unsigned i=0;i<90;++i) {
        image_t image={2048,2048,12,VK_NULL_HANDLE};
        vk_createImageAndBindWithMemory(&image);
    }
    assert(devMemImg.Index==45 && devMemImg.Capacity>=45); // Old fixed array had eight slots.
    image_req.size=80u<<20;
    image_t image={4096,4096,13,VK_NULL_HANDLE};
    vk_createImageAndBindWithMemory(&image);
    assert(devMemImg.Chunks[45].Size==(80u<<20));
    image_req.size=4096; image_req.memoryTypeBits=2;
    vk_createImageAndBindWithMemory(&image);
    assert(devMemImg.Chunks[46].typeIndex==1);
    unsigned chunks=devMemImg.Index;
    vk_createImageAndBindWithMemory(&image);
    assert(devMemImg.Index==chunks);
    for (unsigned i=0;i<devMemImg.Index;++i) qvkFreeMemory(vk.device,devMemImg.Chunks[i].block,NULL);
    free(devMemImg.Chunks); memset(&devMemImg,0,sizeof(devMemImg));
    vk_destroy_staging_buffer(); vk_destroy_staging_buffer();
    assert(allocations==frees);
    puts("PASS: HD staging growth/reuse, 45 image chunks, oversized images, alignment/type matching, cleanup");
}
'''
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', required=True)
p.add_argument('--sdk', type=Path, required=True)
args = p.parse_args()
assert source.count('vk_ensure_staging_buffer(buffer_size);') == 3
for part in source.split('VK_CHECK(qvkMapMemory')[1:]:
    assert 'StagBuf.mappableMem' in part[:80]
assert 'free(devMemImg.Chunks);' in source[source.index('void vk_destroyImageRes'):]
with tempfile.TemporaryDirectory(prefix='vq3-hd-memory-') as directory:
    directory = Path(directory)
    (directory/'production.h').write_text(body)
    (directory/'fixture.c').write_text(fixture)
    exe = directory/'fixture.exe'
    subprocess.run([args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-I'+str(args.sdk/'Include'), str(directory/'fixture.c'), '-o', str(exe)], check=True, timeout=30)
    subprocess.run([str(exe)], check=True, timeout=10)
