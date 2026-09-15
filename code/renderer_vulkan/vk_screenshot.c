#include "tr_globals.h"
#include "tr_cvar.h"
#include "vk_instance.h"
#include "vk_image.h"
#include "vk_cmd.h"
#include "vk_screenshot.h"
#include "vk_postfx.h"

#include "R_ImageProcess.h"
#include "R_ImageJPG.h"
#include "ref_import.h"
#include "glConfig.h"

#include <math.h>
static qboolean screenshot_pending, video_pending, levelshot_pending;
static screenshotCommand_t pending_screenshot;
static videoFrameCommand_t pending_video;
static char pending_filename[MAX_OSPATH];
static void R_LevelShot(int W, int H);

void vk_queue_screenshot(const screenshotCommand_t *cmd)
{
    pending_screenshot = *cmd;
    Q_strncpyz(pending_filename, cmd->fileName, sizeof(pending_filename));
    pending_screenshot.fileName = pending_filename;
    screenshot_pending = qtrue;
}

void vk_queue_video_frame(const videoFrameCommand_t *cmd)
{
    pending_video = *cmd;
    video_pending = qtrue;
}

void vk_reset_captures(void)
{
    screenshot_pending = video_pending = levelshot_pending = qfalse;
}

void vk_flush_captures(void)
{
    int width, height;
    if (!screenshot_pending && !video_pending && !levelshot_pending) return;
    R_GetWinResolution(&width, &height);
    if (screenshot_pending)
        RB_TakeScreenshot(width, height, pending_filename, pending_screenshot.jpeg);
    if (video_pending && pending_video.width == width && pending_video.height == height)
        RB_TakeVideoFrameCmd(&pending_video);
    if (levelshot_pending) R_LevelShot(width, height);
    vk_reset_captures();
}
/* 
============================================================================== 
 
						SCREEN SHOTS 

NOTE TTimo
some thoughts about the screenshots system:
screenshots get written in fs_homepath + fs_gamedir
vanilla q3 .. baseq3/screenshots/ *.tga
team arena .. missionpack/screenshots/ *.tga

two commands: "screenshot" and "screenshotJPEG"
we use statics to store a count and start writing the first screenshot/screenshot????.tga (.jpg) available
(with FS_FileExists / FS_FOpenFileWrite calls)
FIXME: the statics don't get a reinit between fs_game changes


Images created with tiling equal to VK_IMAGE_TILING_LINEAR have further restrictions on their
limits and capabilities compared to images created with tiling equal to VK_IMAGE_TILING_OPTIMAL.
Creation of images with tiling VK_IMAGE_TILING_LINEAR may not be supported unless other parameters
meetall of the constraints:
* imageType is VK_IMAGE_TYPE_2D
* format is not a depth/stencil format
* mipLevels is 1
* arrayLayers is 1
* samples is VK_SAMPLE_COUNT_1_BIT
* usage only includes VK_IMAGE_USAGE_TRANSFER_SRC_BIT and/or VK_IMAGE_USAGE_TRANSFER_DST_BIT
Implementations may support additional limits and capabilities beyond those listed above.

============================================================================== 

*/


// Just reading the pixels from GPU memory, don't care about swizzling.
// Rows come back top-down, matching the on-screen orientation.
static void vk_read_pixels(unsigned char* pBuf, uint32_t W, uint32_t H)
{

	qvkDeviceWaitIdle(vk.device);

	// Create image in host visible memory to serve as a destination for framebuffer pixels.
  
    const uint32_t sizeFB = W * H * 4;
    

	VkBuffer buffer;
    VkDeviceMemory memory;
    {
        VkBufferCreateInfo buffer_create_info;
        memset(&buffer_create_info, 0, sizeof(buffer_create_info));
        buffer_create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_create_info.size = sizeFB;
        buffer_create_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_CHECK( qvkCreateBuffer(vk.device, &buffer_create_info, NULL, &buffer) );

        VkMemoryRequirements memory_requirements;
        qvkGetBufferMemoryRequirements(vk.device, buffer, &memory_requirements);

        VkMemoryAllocateInfo memory_allocate_info;
        memset(&memory_allocate_info, 0, sizeof(memory_allocate_info));
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize = memory_requirements.size;
        //
        memory_allocate_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VK_CHECK( qvkAllocateMemory(vk.device, &memory_allocate_info, NULL, &memory) );
        VK_CHECK( qvkBindBufferMemory(vk.device, buffer, memory, 0) );
    }


    //////////////////////////////////////////////////////////

    VkBufferImageCopy image_copy;
    {
        image_copy.bufferOffset = 0;
        image_copy.bufferRowLength = W;
        image_copy.bufferImageHeight = H;

        image_copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_copy.imageSubresource.layerCount = 1;
        image_copy.imageSubresource.mipLevel = 0;
        image_copy.imageSubresource.baseArrayLayer = 0;
        image_copy.imageOffset.x = 0;
        image_copy.imageOffset.y = 0;
        image_copy.imageOffset.z = 0;
        image_copy.imageExtent.width = W;
        image_copy.imageExtent.height = H;
        image_copy.imageExtent.depth = 1;
    }

    // Memory barriers are used to explicitly control access to buffer and image subresource ranges.
    // Memory barriers are used to transfer ownership between queue families, change image layouts,
    // and define availability and visibility operations. They explicitly define the access types 
    // and buffer and image subresource ranges that are included in the access scopes of a memory
    // dependency that is created by a synchronization command that includes them.
    //
    // Image memory barriers only apply to memory accesses involving a specific image subresource
    // range. That is, a memory dependency formed from an image memory barrier is scoped to access
    // via the specified image subresource range. Image memory barriers can also be used to define
    // image layout transitions or a queue family ownership transfer for the specified image 
    // subresource range.

    VkImageMemoryBarrier image_barrier;
    {
        image_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.pNext = NULL;
        image_barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        image_barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        image_barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        image_barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        image_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        image_barrier.image = vk.swapchain_images_array[vk.idx_swapchain_image];
        image_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        image_barrier.subresourceRange.baseMipLevel = 0;
        image_barrier.subresourceRange.levelCount = 1;
        image_barrier.subresourceRange.baseArrayLayer = 0;
        image_barrier.subresourceRange.layerCount = 1;
    }


    // read pixel with command buffer
    VkCommandBuffer cmdBuf;

    VkCommandBufferAllocateInfo alloc_info;
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.pNext = NULL;
    alloc_info.commandPool = vk.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    VK_CHECK(qvkAllocateCommandBuffers(vk.device, &alloc_info, &cmdBuf));

    VkCommandBufferBeginInfo begin_info;
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext = NULL;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin_info.pInheritanceInfo = NULL;
    VK_CHECK(qvkBeginCommandBuffer(cmdBuf, &begin_info));

    qvkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier); 
    qvkCmdCopyImageToBuffer(cmdBuf, vk.swapchain_images_array[vk.idx_swapchain_image], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &image_copy);
    /* Captures run while this image is still acquired. Restore the layout
     * before presenting it or returning it to Streamline's present hook. */
    image_barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    image_barrier.dstAccessMask = 0;
    image_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    image_barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    qvkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &image_barrier);
    VK_CHECK(qvkEndCommandBuffer(cmdBuf));

    VkSubmitInfo submit_info;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext = NULL;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = NULL;
    submit_info.pWaitDstStageMask = NULL;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmdBuf;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = NULL;
    VK_CHECK(qvkQueueSubmit(vk.queue, 1, &submit_info, VK_NULL_HANDLE));

    VK_CHECK(qvkQueueWaitIdle(vk.queue));

    qvkFreeCommandBuffers(vk.device, vk.command_pool, 1, &cmdBuf);


    // Memory objects created with the memory property VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
    // are considered mappable. Memory objects must be mappable in order to be successfully
    // mapped on the host. 
    //
    // To retrieve a host virtual address pointer to a region of a mappable memory object
    unsigned char* data;
    VK_CHECK(qvkMapMemory(vk.device, memory, 0, VK_WHOLE_SIZE, 0, (void**)&data));
    memcpy(pBuf, data, sizeFB);
    qvkUnmapMemory(vk.device, memory);
    qvkFreeMemory(vk.device, memory, NULL);
    qvkDestroyBuffer(vk.device, buffer, NULL);
}

extern void RE_SaveJPG(char * filename, int quality, int image_width, int image_height, unsigned char *image_buffer, int padding);



// ====================================================================
// Minimal self-contained PNG writer (stored DEFLATE blocks, no zlib
// dependency). Hand-rolled CRC32 + Adler32. Emits a valid 8-bit RGB PNG.
// ====================================================================

static const uint32_t crc32_table[256] =
{
    0x00000000u,0x77073096u,0xEE0E612Cu,0x990951BAu,0x076DC419u,0x706AF48Fu,0xE963A535u,0x9E6495A3u,
    0x0EDB8832u,0x79DCB8A4u,0xE0D5E91Eu,0x97D2D988u,0x09B64C2Bu,0x7EB17CBDu,0xE7B82D07u,0x90BF1D91u,
    0x1DB71064u,0x6AB020F2u,0xF3B97148u,0x84BE41DEu,0x1ADAD47Du,0x6DDDE4EBu,0xF4D4B551u,0x83D385C7u,
    0x136C9856u,0x646BA8C0u,0xFD62F97Au,0x8A65C9ECu,0x14015C4Fu,0x63066CD9u,0xFA0F3D63u,0x8D080DF5u,
    0x3B6E20C8u,0x4C69105Eu,0xD56041E4u,0xA2677172u,0x3C03E4D1u,0x4B04D447u,0xD20D85FDu,0xA50AB56Bu,
    0x35B5A8FAu,0x42B2986Cu,0xDBBBC9D6u,0xACBCF940u,0x32D86CE3u,0x45DF5C75u,0xDCD60DCFu,0xABD13D59u,
    0x26D930ACu,0x51DE003Au,0xC8D75180u,0xBFD06116u,0x21B4F4B5u,0x56B3C423u,0xCFBA9599u,0xB8BDA50Fu,
    0x2802B89Eu,0x5F058808u,0xC60CD9B2u,0xB10BE924u,0x2F6F7C87u,0x58684C11u,0xC1611DABu,0xB6662D3Du,
    0x76DC4190u,0x01DB7106u,0x98D220BCu,0xEFD5102Au,0x71B18589u,0x06B6B51Fu,0x9FBFE4A5u,0xE8B8D433u,
    0x7807C9A2u,0x0F00F934u,0x9609A88Eu,0xE10E9818u,0x7F6A0DBBu,0x086D3D2Du,0x91646C97u,0xE6635C01u,
    0x6B6B51F4u,0x1C6C6162u,0x856530D8u,0xF262004Eu,0x6C0695EDu,0x1B01A57Bu,0x8208F4C1u,0xF50FC457u,
    0x65B0D9C6u,0x12B7E950u,0x8BBEB8EAu,0xFCB9887Cu,0x62DD1DDFu,0x15DA2D49u,0x8CD37CF3u,0xFBD44C65u,
    0x4DB26158u,0x3AB551CEu,0xA3BC0074u,0xD4BB30E2u,0x4ADFA541u,0x3DD895D7u,0xA4D1C46Du,0xD3D6F4FBu,
    0x4369E96Au,0x346ED9FCu,0xAD678846u,0xDA60B8D0u,0x44042D73u,0x33031DE5u,0xAA0A4C5Fu,0xDD0D7CC9u,
    0x5005713Cu,0x270241AAu,0xBE0B1010u,0xC90C2086u,0x5768B525u,0x206F85B3u,0xB966D409u,0xCE61E49Fu,
    0x5EDEF90Eu,0x29D9C998u,0xB0D09822u,0xC7D7A8B4u,0x59B33D17u,0x2EB40D81u,0xB7BD5C3Bu,0xC0BA6CADu,
    0xEDB88320u,0x9ABFB3B6u,0x03B6E20Cu,0x74B1D29Au,0xEAD54739u,0x9DD277AFu,0x04DB2615u,0x73DC1683u,
    0xE3630B12u,0x94643B84u,0x0D6D6A3Eu,0x7A6A5AA8u,0xE40ECF0Bu,0x9309FF9Du,0x0A00AE27u,0x7D079EB1u,
    0xF00F9344u,0x8708A3D2u,0x1E01F268u,0x6906C2FEu,0xF762575Du,0x806567CBu,0x196C3671u,0x6E6B06E7u,
    0xFED41B76u,0x89D32BE0u,0x10DA7A5Au,0x67DD4ACCu,0xF9B9DF6Fu,0x8EBEEFF9u,0x17B7BE43u,0x60B08ED5u,
    0xD6D6A3E8u,0xA1D1937Eu,0x38D8C2C4u,0x4FDFF252u,0xD1BB67F1u,0xA6BC5767u,0x3FB506DDu,0x48B2364Bu,
    0xD80D2BDAu,0xAF0A1B4Cu,0x36034AF6u,0x41047A60u,0xDF60EFC3u,0xA867DF55u,0x316E8EEFu,0x4669BE79u,
    0xCB61B38Cu,0xBC66831Au,0x256FD2A0u,0x5268E236u,0xCC0C7795u,0xBB0B4703u,0x220216B9u,0x5505262Fu,
    0xC5BA3BBEu,0xB2BD0B28u,0x2BB45A92u,0x5CB36A04u,0xC2D7FFA7u,0xB5D0CF31u,0x2CD99E8Bu,0x5BDEAE1Du,
    0x9B64C2B0u,0xEC63F226u,0x756AA39Cu,0x026D930Au,0x9C0906A9u,0xEB0E363Fu,0x72076785u,0x05005713u,
    0x95BF4A82u,0xE2B87A14u,0x7BB12BAEu,0x0CB61B38u,0x92D28E9Bu,0xE5D5BE0Du,0x7CDCEFB7u,0x0BDBDF21u,
    0x86D3D2D4u,0xF1D4E242u,0x68DDB3F8u,0x1FDA836Eu,0x81BE16CDu,0xF6B9265Bu,0x6FB077E1u,0x18B74777u,
    0x88085AE6u,0xFF0F6A70u,0x66063BCAu,0x11010B5Cu,0x8F659EFFu,0xF862AE69u,0x616BFFD3u,0x166CCF45u,
    0xA00AE278u,0xD70DD2EEu,0x4E048354u,0x3903B3C2u,0xA7672661u,0xD06016F7u,0x4969474Du,0x3E6E77DBu,
    0xAED16A4Au,0xD9D65ADCu,0x40DF0B66u,0x37D83BF0u,0xA9BCAE53u,0xDEBB9EC5u,0x47B2CF7Fu,0x30B5FFE9u,
    0xBDBDF21Cu,0xCABAC28Au,0x53B39330u,0x24B4A3A6u,0xBAD03605u,0xCDD70693u,0x54DE5729u,0x23D967BFu,
    0xB3667A2Eu,0xC4614AB8u,0x5D681B02u,0x2A6F2B94u,0xB40BBE37u,0xC30C8EA1u,0x5A05DF1Bu,0x2D02EF8Du
};

static void wr_u32( unsigned char* b, uint32_t v )
{
    b[0] = (unsigned char)((v >> 24) & 0xFF);
    b[1] = (unsigned char)((v >> 16) & 0xFF);
    b[2] = (unsigned char)((v >> 8) & 0xFF);
    b[3] = (unsigned char)(v & 0xFF);
}

static void wr_chunk( unsigned char** pp, const char* type, const unsigned char* data, size_t n )
{
    unsigned char* p = *pp;
    // length
    wr_u32( p, (uint32_t)n ); p += 4;
    memcpy( p, type, 4 ); p += 4;
    memcpy( p, data, n ); p += n;

    unsigned char crcbuf[4];
    uint32_t crc = 0xFFFFFFFFu;
    {
        size_t i;
        for (i = 0; i < 4; i++)
            crc = (crc >> 8) ^ crc32_table[(crc ^ (unsigned char)type[i]) & 0xFFu];
        for (i = 0; i < n; i++)
            crc = (crc >> 8) ^ crc32_table[(crc ^ data[i]) & 0xFFu];
    }
    crc ^= 0xFFFFFFFFu;
    wr_u32( crcbuf, crc );

    memcpy( p, crcbuf, 4 ); p += 4;
    *pp = p;
}

void RE_SavePNG( const char *fileName, int width, int height, unsigned char *pImg )
{
    // pImg is packed RGB (3 bytes/px), non-interlaced, no filter per scanline
    const size_t rowBytes = (size_t)width * 3;
    const size_t stride   = rowBytes + 1;              // +1 filter byte
    const size_t rawBytes = stride * (size_t)height;

    // Stored DEFLATE blocks: 5-byte header + up to 65535 raw bytes each.
    size_t nBlocks = (rawBytes + 65535u - 1u) / 65535u;
    if (nBlocks == 0) nBlocks = 1;
    const size_t zlibLen = 2 + (5 * nBlocks + rawBytes) + 4;   // hdr + stored data + adler

    const size_t ihdrLen = 13;
    const size_t total = 8 + (12 + ihdrLen) + (12 + zlibLen) + 12;  // sig+IHDR+IDAT+IEND

    // Every scanline of the deflate stream is prefixed by its PNG filter type
    // byte (0 = None). pImg is tightly packed RGB, so synthesize the filtered
    // stream into its own buffer first.
    unsigned char* const raw = (unsigned char*) malloc ( rawBytes );
    {
        size_t y;
        for (y = 0; y < (size_t)height; y++)
        {
            raw[y * stride] = 0;
            memcpy( raw + y * stride + 1, pImg + y * rowBytes, rowBytes );
        }
    }

    unsigned char* const buf = (unsigned char*) malloc ( total );
    unsigned char* p = buf;

    // PNG signature
    static const unsigned char sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    memcpy( p, sig, 8 ); p += 8;

    // IHDR
    {
        unsigned char ihdr[13];
        wr_u32( ihdr + 0, (uint32_t)width );
        wr_u32( ihdr + 4, (uint32_t)height );
        ihdr[8]  = 8;           // bit depth
        ihdr[9]  = 2;           // color type: truecolor RGB
        ihdr[10] = 0;           // compression
        ihdr[11] = 0;           // filter
        ihdr[12] = 0;           // interlace
        wr_chunk( &p, "IHDR", ihdr, ihdrLen );
    }

    // IDAT — zlib stream (stored DEFLATE blocks) of scanline data
    {
        const size_t idatLen = zlibLen;
        unsigned char* idat = (unsigned char*) malloc ( idatLen );
        unsigned char* q = idat;

        // zlib header: CM=8 (deflate), CINFO=7 (32K window), FLEVEL=0
        q[0] = 0x78; q[1] = 0x01; q += 2;

        // build the raw deflate payload first so we can compute Adler32
        {
            const size_t payloadLen = 5 * nBlocks + rawBytes;
            unsigned char* payload = (unsigned char*) malloc ( payloadLen );
            unsigned char* r = payload;

            const unsigned char* src = raw;
            size_t left = rawBytes;

            while (left > 0)
            {
                size_t len = (left > 65535) ? 65535 : left;
                int final = (len == left) ? 1 : 0;

                r[0] = (unsigned char)final;          // BFINAL + BTYPE=00 (stored)
                r[1] = (unsigned char)(len & 0xFF);        // LEN
                r[2] = (unsigned char)((len >> 8) & 0xFF); // LEN
                r[3] = (unsigned char)((~len) & 0xFF);        // NLEN
                r[4] = (unsigned char)(((~len) >> 8) & 0xFF); // NLEN
                r += 5;

                memcpy( r, src, len );
                r += len;
                src += len;
                left -= len;
            }

            memcpy( q, payload, payloadLen ); q += payloadLen;
            free( payload );
        }

        // Adler32 of the raw scanline data
        {
            uint32_t a = 1, b = 0;
            const unsigned char* s = raw;
            size_t i;
            for (i = 0; i < rawBytes; i++)
            {
                a = (a + s[i]) % 65521u;
                b = (b + a) % 65521u;
            }
            uint32_t adler = (b << 16) | a;
            q[0] = (unsigned char)((adler >> 24) & 0xFF);
            q[1] = (unsigned char)((adler >> 16) & 0xFF);
            q[2] = (unsigned char)((adler >> 8) & 0xFF);
            q[3] = (unsigned char)(adler & 0xFF);
            q += 4;
            free( raw );
        }

        wr_chunk( &p, "IDAT", idat, idatLen );
        free( idat );
    }

    // IEND
    {
        wr_chunk( &p, "IEND", (const unsigned char*)"", 0 );
    }

    ri.FS_WriteFile( (char*)fileName, buf, (int)total );
    free( buf );
}

// ====================================================================
// Photo mode: write a companion .json settings file next to a
// screenshot (map, camera pose, and every post-processing / exposure /
// lighting cvar). Small hand-rolled JSON emitters, no external deps.
// ====================================================================

static void JS_append_value(char *buf, int bufsize, int *n, qboolean *first, const char *key, const char *value)
{
    char escaped[256];
    const char *s = value;
    char *d = escaped;
    int r, max = (int)sizeof(escaped) - 1;
    while (*s && d - escaped < max) {
        if ((*s == '"' || *s == '\\') && d - escaped >= max - 1) break;
        if (*s == '"' || *s == '\\') *d++ = '\\';
        *d++ = *s++;
    }
    *d = '\0';
    if (*n >= bufsize - 1) return;
    r = Com_sprintf(buf + *n, bufsize - *n, "%s\"%s\":\"%s\"", *first ? "" : ",", key, escaped);
    if (r > 0) *n += r;
    if (*n > bufsize - 1) *n = bufsize - 1;
    *first = qfalse;
}

static void JS_append_number(char *buf, int bufsize, int *n, qboolean *first, const char *key, double value)
{
    char num[64];
    int r;
    Com_sprintf(num, sizeof(num), "%.9g", value);
    if (*n >= bufsize - 1) return;
    r = Com_sprintf(buf + *n, bufsize - *n, "%s\"%s\":%s", *first ? "" : ",", key, num);
    if (r > 0) *n += r;
    if (*n > bufsize - 1) *n = bufsize - 1;
    *first = qfalse;
}

static void JS_append_array3(char *buf, int bufsize, int *n, qboolean *first, const char *key, const float v[3])
{
    int r;
    if (*n >= bufsize - 1) return;
    r = Com_sprintf(buf + *n, bufsize - *n, "%s\"%s\":[%.9g,%.9g,%.9g]",
        *first ? "" : ",", key, v[0], v[1], v[2]);
    if (r > 0) *n += r;
    if (*n > bufsize - 1) *n = bufsize - 1;
    *first = qfalse;
}

static void JS_append_open(char *buf, int bufsize, int *n, qboolean *first, const char *key)
{
    int r;
    if (*n >= bufsize - 1) return;
    r = Com_sprintf(buf + *n, bufsize - *n, "%s\"%s\":{", *first ? "" : ",", key);
    if (r > 0) *n += r;
    if (*n > bufsize - 1) *n = bufsize - 1;
    *first = qfalse;
}

static void JS_append_close(char *buf, int bufsize, int *n)
{
    if (*n >= bufsize - 1) return;
    buf[(*n)++] = '}';
    buf[*n] = '\0';
}

static const char * const photoNVCvars[] = {
    "r_nvNightVision", "r_nvTint", "r_nvGrain", "r_nvVignette",
    "r_nvBrightness", "r_nvOverride", "r_nvDebug",
};
static const char * const photoExposureCvars[] = {
    "r_pathTracingExposure", "r_pathTracingAutoExposure",
    "r_pathTracingAdaptiveSpeed", "r_pathTracingAdaptiveTarget",
    "r_pathTracingAdaptiveMin", "r_pathTracingAdaptiveMax",
    "r_pathTracingReference",
};
static const char * const photoLightingCvars[] = {
    "r_pathTracingSunAngle", "r_pathTracingSunScale",
    "r_pathTracingAmbient", "r_pathTracingLightRadius",
    "r_rayTracingShadowStrength", "r_rayTracingShadowBias",
    "r_muzzleFlashBrightness", "r_rocketBrightness",
    "r_rocketLightScale", "r_rocketExplosionLightScale",
    "r_lightningGunLightScale", "r_muzzleFlashLightScale",
};
static const char * const photoRendererCvars[] = {
    "r_rayTracing", "r_pathTracingScale", "r_pathTracingSamples",
    "r_pathTracingLightReuse", "r_pathTracingAdaptive",
    "r_pathTracingAdaptiveDebug", "r_pathTracingBounces",
    "r_pathTracingDenoise", "r_pathTracingTemporal",
    "r_pathTracingHistory",
};

static void JS_append_cvars(char *buf, int bufsize, int *n, qboolean *first,
    const char * const *names, int count)
{
    int i;
    for (i = 0; i < count; ++i) {
        cvar_t *cvar = ri.Cvar_Get(names[i], "", 0);
        JS_append_value(buf, bufsize, n, first, names[i],
            cvar ? cvar->string : "0");
    }
}

static void RB_WritePhotoSettings( const char *imagePath )
{
    static char jsonBuf[16384];
    char jsonPath[MAX_OSPATH];
    const float *f;
    float pitch, yaw;
    qboolean first;
    int n;
    int i, len;
    cvar_t *master = ri.Cvar_Get( "r_postfx", "0", 0 );

    Q_strncpyz( jsonPath, imagePath, sizeof( jsonPath ) );
    len = strlen( jsonPath );
    for ( i = len - 1 ; i >= 0 ; i-- ) {
        if ( jsonPath[i] == '.' ) {
            jsonPath[i] = '\0';
            break;
        }
        if ( jsonPath[i] == '/' || jsonPath[i] == '\\' ) break;
    }
    Q_strcat( jsonPath, sizeof( jsonPath ), ".json" );

    // Recover the world-space view angles from the forward axis vector.
    // AngleVectors(yaw,pitch) produces forward = (cp*cy, cp*sy, -sp).
    f = tr.viewParms.or.axis[0];
    pitch = RAD2DEG( asin( -f[2] ) );
    yaw = RAD2DEG( atan2( f[1], f[0] ) );

    first = qtrue;
    n = 1;
    jsonBuf[0] = '{';

    JS_append_value( jsonBuf, sizeof( jsonBuf ), &n, &first, "map",
        tr.world ? tr.world->baseName : "" );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "width", tr.viewParms.viewportWidth );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "height", tr.viewParms.viewportHeight );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "fov", tr.viewParms.fovX );
    JS_append_array3( jsonBuf, sizeof( jsonBuf ), &n, &first, "cameraOrigin", tr.viewParms.or.origin );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "cameraPitch", pitch );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "cameraYaw", yaw );
    JS_append_number( jsonBuf, sizeof( jsonBuf ), &n, &first, "cameraRoll", 0 );

    // Post-processing: local effect chain (r_postfx + r_fx_*) plus NV cvars.
    JS_append_open( jsonBuf, sizeof( jsonBuf ), &n, &first, "postfx" );
    {
        qboolean inner = qtrue;
        int innerN = n;
        JS_append_value( jsonBuf, sizeof( jsonBuf ), &innerN, &inner, "r_postfx",
            master ? master->string : "0" );
        vk_postfx_write_json_cvars( jsonBuf, sizeof( jsonBuf ), &innerN, &inner );
        JS_append_cvars( jsonBuf, sizeof( jsonBuf ), &innerN, &inner, photoNVCvars, 7 );
        JS_append_close( jsonBuf, sizeof( jsonBuf ), &innerN );
        n = innerN;
    }

    // Exposure.
    JS_append_open( jsonBuf, sizeof( jsonBuf ), &n, &first, "exposure" );
    {
        qboolean inner = qtrue;
        int innerN = n;
        JS_append_cvars( jsonBuf, sizeof( jsonBuf ), &innerN, &inner, photoExposureCvars, ARRAY_LEN( photoExposureCvars ) );
        JS_append_close( jsonBuf, sizeof( jsonBuf ), &innerN );
        n = innerN;
    }

    // Lighting / sun / weapon light scales.
    JS_append_open( jsonBuf, sizeof( jsonBuf ), &n, &first, "lighting" );
    {
        qboolean inner = qtrue;
        int innerN = n;
        JS_append_cvars( jsonBuf, sizeof( jsonBuf ), &innerN, &inner, photoLightingCvars, ARRAY_LEN( photoLightingCvars ) );
        JS_append_close( jsonBuf, sizeof( jsonBuf ), &innerN );
        n = innerN;
    }

    // Renderer / path tracing pipeline.
    JS_append_open( jsonBuf, sizeof( jsonBuf ), &n, &first, "renderer" );
    {
        qboolean inner = qtrue;
        int innerN = n;
        JS_append_cvars( jsonBuf, sizeof( jsonBuf ), &innerN, &inner, photoRendererCvars, ARRAY_LEN( photoRendererCvars ) );
        JS_append_close( jsonBuf, sizeof( jsonBuf ), &innerN );
        n = innerN;
    }

    JS_append_close( jsonBuf, sizeof( jsonBuf ), &n );
    if (n > (int)sizeof(jsonBuf) - 2) n = (int)sizeof(jsonBuf) - 2;
    jsonBuf[n++] = '\n';
    jsonBuf[n] = '\0';

    ri.FS_WriteFile( jsonPath, jsonBuf, n );
    ri.Printf( PRINT_ALL, "Wrote %s\n", jsonPath );
}


void RB_TakeScreenshot( int width, int height, char *fileName, VkBool32 isJpeg)
{
    ri.Printf(PRINT_ALL, "read %dx%d pixels from GPU\n", width, height);
    if ( ri.Cvar_VariableIntegerValue( "cg_photoMode" ) ) {
        RB_WritePhotoSettings( fileName );
    }
    const uint32_t cnPixels = width * height; 

    if(isJpeg)
    {
        
        //unsigned char *buffer;
        //size_t offset = 0, memcount;
        //int padlen;
        //memcount = (width * 3 + padlen) * height;
        //RE_SaveJPG(fileName, 90, width, height, buffer + offset, padlen);
        unsigned char* const pImg = (unsigned char*) malloc ( cnPixels * 4);
        vk_read_pixels(pImg, width, height);
        {
            unsigned char* pSrc = pImg;
            unsigned char* pDst = pImg;
            uint32_t i;
            for (i = 0; i < cnPixels; i++)
            {
                pSrc[0] = pDst[2];
                pSrc[1] = pDst[1];
                pSrc[2] = pDst[0];
                pSrc += 3;
                pDst += 4;
            }
        }
        RE_SaveJPG(fileName, 90, width, height, pImg, 0);

        free( pImg );

        //bufSize = RE_SaveJPGToBuffer(out, bufSize, 90, width, height, pImg, padding);
        //ri.FS_WriteFile(filename, out, bufSize);
    }
    else if ( strstr( fileName, ".png" ) )
    {
        // PNG screenshot — reuse the same RGB readback, then encode
        // a valid PNG (stored DEFLATE blocks, hand-rolled CRC32/Adler32).
        unsigned char* const pImg = (unsigned char*) malloc ( cnPixels * 4);

        vk_read_pixels(pImg, width, height);

        // Remove alpha channel and rbg <-> bgr
        {
            unsigned char* pSrc = pImg;
            unsigned char* pDst = pImg;

            uint32_t i;
            for (i = 0; i < cnPixels; i++)
            {
                pSrc[0] = pDst[2];
                pSrc[1] = pDst[1];
                pSrc[2] = pDst[0];
                pSrc += 3;
                pDst += 4;
            }
        }

        RE_SavePNG(fileName, width, height, pImg);
        free( pImg );
    }
    else
    {

        //const uint32_t cnPixels = width * height;
        const uint32_t imgSize = 18 + cnPixels * 3;

        unsigned char* const pBuffer = (unsigned char*) malloc ( imgSize + cnPixels * 4 );
        unsigned char* const buffer_ptr = pBuffer + 18;
        unsigned char* const pImg = pBuffer + imgSize;
        
        vk_read_pixels(pImg, width, height);

        // the readback rows run top-down already, so no Y flip is applied.
        // the TGA descriptor byte needs the top-left origin bit to match.
        memset (pBuffer, 0, 18);
        pBuffer[2] = 2;		// uncompressed type
        pBuffer[12] = width & 255;
        pBuffer[13] = width >> 8;
        pBuffer[14] = height & 255;
        pBuffer[15] = height >> 8;
        pBuffer[16] = 24;	// pixel size
        pBuffer[17] = 32;	// top-left origin, 0 attribute bits

        //    VkBool32 need_swizzle = ( 
        //            vk.surface_format.format == VK_FORMAT_B8G8R8A8_SRGB ||
        //            vk.surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
        //           vk.surface_format.format == VK_FORMAT_B8G8R8A8_SNORM );

        uint32_t i;
        if (0)
        {
            for (i = 0; i < cnPixels; i++)
            {
                buffer_ptr[i*3]   = *(pImg + i*4 + 2);
                buffer_ptr[i*3+1] = *(pImg + i*4 + 1);
                buffer_ptr[i*3+2] = *(pImg + i*4 );;
            }
        }
        else
        {
            for (i = 0; i < cnPixels; i++)
            {
                buffer_ptr[i*3]   = *(pImg + i*4 );
                buffer_ptr[i*3+1] = *(pImg + i*4 + 1);
                buffer_ptr[i*3+2] = *(pImg + i*4 + 2);
            }
        }
        ri.FS_WriteFile( fileName, pBuffer, imgSize);
        
        free( pBuffer );
    }
}


static void R_TakeScreenshot( int x, int y, int width, int height, char *name, qboolean jpeg )
{
	static char	fileName[MAX_OSPATH] = {0}; // bad things if two screenshots per frame?
	
    screenshotCommand_t	*cmd = (screenshotCommand_t*) R_GetCommandBuffer(sizeof(*cmd));
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SCREENSHOT;

	cmd->x = x;
	cmd->y = y;
	cmd->width = width;
	cmd->height = height;
	
    //Q_strncpyz( fileName, name, sizeof(fileName) );

    strncpy(fileName, name, sizeof(fileName));

	cmd->fileName = fileName;
	cmd->jpeg = jpeg;
}



/*
====================
R_LevelShot

levelshots are specialized 128*128 thumbnails for the 
menu system, sampled down from full screen distorted images
====================
*/
static void R_LevelShot( int W, int H )
{
	char checkname[MAX_OSPATH];
	unsigned char* buffer;
	unsigned char* source;
	unsigned char* src;
	unsigned char* dst;
	int			x, y;
	int			r, g, b;
	float		xScale, yScale;
	int			xx, yy;
    int i = 0;
	sprintf( checkname, "levelshots/%s.tga", tr.world->baseName );

	source = (unsigned char*) ri.Hunk_AllocateTempMemory( W * H * 3 );

	buffer = (unsigned char*) ri.Hunk_AllocateTempMemory( 128 * 128*3 + 18);
	memset (buffer, 0, 18);
	buffer[2] = 2;		// uncompressed type
	buffer[12] = 128;
	buffer[14] = 128;
	buffer[16] = 24;	// pixel size

    {
        unsigned char* buffer2 = (unsigned char*) malloc (W * H * 4);
        vk_read_pixels(buffer2, W, H);

        unsigned char* buffer_ptr = source;
        unsigned char* buffer2_ptr = buffer2;
        for (i = 0; i < W * H; i++)
        {
            buffer_ptr[0] = buffer2_ptr[0];
            buffer_ptr[1] = buffer2_ptr[1];
            buffer_ptr[2] = buffer2_ptr[2];
            buffer_ptr += 3;
            buffer2_ptr += 4;
        }
        free(buffer2);
    }

	// resample from source
	xScale = W / 512.0f;
	yScale = H / 384.0f;
	for ( y = 0 ; y < 128 ; y++ ) {
		for ( x = 0 ; x < 128 ; x++ ) {
			r = g = b = 0;
			for ( yy = 0 ; yy < 3 ; yy++ ) {
				for ( xx = 0 ; xx < 4 ; xx++ ) {
					src = source + 3 * ( W * (int)( (y*3+yy)*yScale ) + (int)( (x*4+xx)*xScale ) );
					r += src[0];
					g += src[1];
					b += src[2];
				}
			}
			dst = buffer + 18 + 3 * ( y * 128 + x );
			dst[0] = b / 12;
			dst[1] = g / 12;
			dst[2] = r / 12;
		}
	}

	ri.FS_WriteFile( checkname, buffer, 128 * 128*3 + 18 );

	ri.Hunk_FreeTempMemory( buffer );
	ri.Hunk_FreeTempMemory( source );

	ri.Printf( PRINT_ALL, "Wrote %s\n", checkname );
}

/* 
================== 
R_ScreenShot_f

screenshot
screenshot [silent]
screenshot [levelshot]
screenshot [filename]

Doesn't print the pacifier message if there is a second arg
================== 
*/  
void R_ScreenShot_f (void)
{
	char	checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

    int W;
    int H;

    R_GetWinResolution(&W, &H);


	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) )
    {
		levelshot_pending = qtrue;
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) )
    {
		silent = qtrue;
	}
    else
    {
		silent = qfalse;
	}


	if ( ri.Cmd_Argc() == 2 && !silent )
    {
		// explicit filename
		snprintf( checkname, sizeof(checkname), "screenshots/%s.tga", ri.Cmd_Argv( 1 ) );
	}
    else
    {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan again, 
        // because recording demo avis can involve thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ )
        {
			//R_ScreenshotFilename( lastNumber, checkname );
            
            int	a,b,c,d;

            a = lastNumber / 1000;
            b = lastNumber % 1000 / 100;
            c = lastNumber % 100  / 10;
            d = lastNumber % 10;

            snprintf( checkname, sizeof(checkname), "screenshots/shot%i%i%i%i.tga", a, b, c, d );

            if (!ri.FS_FileExists( checkname ))
            {
                break; // file doesn't exist
            }
		}

		if ( lastNumber >= 9999 )
        {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n"); 
			return;
 		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, W, H, checkname, qfalse );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
} 


void R_ScreenShotJPEG_f(void)
{
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

    int W;
    int H;

    R_GetWinResolution(&W, &H);

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		levelshot_pending = qtrue;
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		snprintf( checkname, sizeof(checkname), "screenshots/%s.jpg", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename

		// if we have saved a previous screenshot, don't scan
		// again, because recording demo avis can involve
		// thousands of shots
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		// scan for a free number
		for ( ; lastNumber <= 9999 ; lastNumber++ )
        {
            int	a,b,c,d;

            a = lastNumber / 1000;
            b = lastNumber % 1000 / 100;
            c = lastNumber % 100  / 10;
            d = lastNumber % 10;

            snprintf( checkname, sizeof(checkname), "screenshots/shot%i%i%i%i.jpg"
                    , a, b, c, d );

            if (!ri.FS_FileExists( checkname ))
            {
                break; // file doesn't exist
            }
		}

		if ( lastNumber >= 9999 )
        {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, W, H, checkname, qtrue );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
}


void R_ScreenShotPNG_f(void)
{
	char		checkname[MAX_OSPATH];
	static	int	lastNumber = -1;
	qboolean	silent;

    int W;
    int H;

    R_GetWinResolution(&W, &H);

	if ( !strcmp( ri.Cmd_Argv(1), "levelshot" ) ) {
		levelshot_pending = qtrue;
		return;
	}

	if ( !strcmp( ri.Cmd_Argv(1), "silent" ) ) {
		silent = qtrue;
	} else {
		silent = qfalse;
	}

	if ( ri.Cmd_Argc() == 2 && !silent ) {
		// explicit filename
		snprintf( checkname, sizeof(checkname), "screenshots/%s.png", ri.Cmd_Argv( 1 ) );
	} else {
		// scan for a free filename
		if ( lastNumber == -1 ) {
			lastNumber = 0;
		}
		for ( ; lastNumber <= 9999 ; lastNumber++ )
        {
            int	a,b,c,d;

            a = lastNumber / 1000;
            b = lastNumber % 1000 / 100;
            c = lastNumber % 100  / 10;
            d = lastNumber % 10;

            snprintf( checkname, sizeof(checkname), "screenshots/shot%i%i%i%i.png"
                    , a, b, c, d );

            if (!ri.FS_FileExists( checkname ))
            {
                break; // file doesn't exist
            }
		}

		if ( lastNumber >= 9999 )
        {
			ri.Printf (PRINT_ALL, "ScreenShot: Couldn't create a file\n");
			return;
		}

		lastNumber++;
	}

	R_TakeScreenshot( 0, 0, W, H, checkname, qfalse );

	if ( !silent ) {
		ri.Printf (PRINT_ALL, "Wrote %s\n", checkname);
	}
}




void RB_TakeVideoFrameCmd( const videoFrameCommand_t * const cmd )
{

	size_t				memcount, linelen;
	int				padwidth, avipadwidth, padlen;
	

	linelen = cmd->width * 3;

	// Alignment stuff for glReadPixels
	padwidth = PAD(linelen, 4);
	padlen = padwidth - linelen;
	// AVI line padding
	avipadwidth = PAD(linelen, 4);

		
    unsigned char* const pImg = (unsigned char*) malloc ( cmd->width * cmd->height * 4);
    
    vk_read_pixels(pImg, cmd->width, cmd->height);

	memcount = padwidth * cmd->height;


	if(cmd->motionJpeg)
	{

        const uint32_t cnPixels = cmd->width * cmd->height;
        unsigned char* pSrc = pImg;
        const unsigned char* pDst = pImg;

        uint32_t i;
        for (i = 0; i < cnPixels; i++)
        {
            pSrc[0] = pDst[2];
            pSrc[1] = pDst[1];
            pSrc[2] = pDst[0];
            pSrc += 3;
            pDst += 4;
        }

        
		memcount = RE_SaveJPGToBuffer(cmd->encodeBuffer, linelen * cmd->height,
			90,	cmd->width, cmd->height, pImg, padlen);
        
		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, memcount);
	}
	else
	{

        unsigned char* buffer_ptr = cmd->encodeBuffer;
        const unsigned char* buffer2_ptr = pImg;

        uint32_t i;
        for (i = 0; i < cmd->width * cmd->height; i++)
        {
            buffer_ptr[0] = buffer2_ptr[0];
            buffer_ptr[1] = buffer2_ptr[1];
            buffer_ptr[2] = buffer2_ptr[2];
            buffer_ptr += 3;
            buffer2_ptr += 4;
        }

		ri.CL_WriteAVIVideoFrame(cmd->encodeBuffer, avipadwidth * cmd->height);
	}


    free(pImg);

}


void RE_TakeVideoFrame( int width, int height, unsigned char *captureBuffer, unsigned char *encodeBuffer, qboolean motionJpeg )
{
	if( !tr.registered ) {
		return;
	}

	videoFrameCommand_t	* cmd = R_GetCommandBuffer( sizeof( *cmd ) );
	if( !cmd ) {
		return;
	}

	cmd->commandId = RC_VIDEOFRAME;

	cmd->width = width;
	cmd->height = height;
	cmd->captureBuffer = captureBuffer;
	cmd->encodeBuffer = encodeBuffer;
	cmd->motionJpeg = motionJpeg;
}
