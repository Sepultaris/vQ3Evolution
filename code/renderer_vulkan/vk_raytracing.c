#include "tr_local.h"
#include "tr_globals.h"
#include "tr_shader.h"
#include "tr_cvar.h"
#include "vk_instance.h"
#include "vk_image.h"
#include "vk_raytracing.h"
#include "vk_pathtrace.h"
#include "R_Parser.h"
#include <float.h>
#include <string.h>

#define RT_MAX_VERTICES (768u * 1024u)
#define RT_MAX_INDICES  (512u * 1024u)

typedef struct {
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkBuffer upload;
	VkDeviceMemory upload_memory;
	void *upload_mapped;
	VkDeviceAddress address;
	VkDeviceSize size;
	void *mapped;
} rt_buffer_t;

typedef struct {
	VkAccelerationStructureKHR handle;
	rt_buffer_t storage;
	VkDeviceSize capacity;
} rt_acceleration_structure_t;

typedef struct {
	qboolean device_supported;
	qboolean path_supported;
	qboolean initialized;
	qboolean overflow_warned;
	qboolean active_logged;
	qboolean path_frozen;
	uint32_t width;
	uint32_t height;
	uint32_t vertex_count;
	uint32_t index_count;
	uint32_t world_vertex_count;
	uint32_t world_index_count;
	float world_ray_distance;
	float *world_vertices;
	uint32_t *world_indices;
	rt_buffer_t vertices;
	rt_buffer_t indices;
	rt_buffer_t instances;
	rt_buffer_t scratch;
	rt_acceleration_structure_t blas;
	rt_acceleration_structure_t world_blas, weapon_blas;
	rt_acceleration_structure_t opaque_blas, opaque_weapon_blas;
	int dynamic_opaque_logged;
	qboolean world_built;
	qboolean world_upload_pending;
	VkDeviceSize scene_upload_bytes;
	uint32_t world_opaque_indices;
	rt_acceleration_structure_t tlas;
	VkSampler sampler;
	VkDescriptorPool descriptor_pool;
	VkDescriptorSetLayout set_layout;
	VkDescriptorSet descriptor_set;
	VkPipelineLayout pipeline_layout;
	VkPipeline pipeline;
	PFN_vkGetBufferDeviceAddress get_buffer_address;
	PFN_vkCreateAccelerationStructureKHR create_as;
	PFN_vkDestroyAccelerationStructureKHR destroy_as;
	PFN_vkGetAccelerationStructureBuildSizesKHR get_build_sizes;
	PFN_vkCmdBuildAccelerationStructuresKHR cmd_build_as;
	PFN_vkGetAccelerationStructureDeviceAddressKHR get_as_address;
} rt_state_t;

static rt_state_t rt;
static VkPhysicalDeviceBufferDeviceAddressFeatures rt_bda_features;
static VkPhysicalDeviceAccelerationStructureFeaturesKHR rt_as_features;
static VkPhysicalDeviceRayQueryFeaturesKHR rt_query_features;
static VkPhysicalDeviceDescriptorIndexingFeatures rt_texture_features;
static VkPhysicalDeviceShaderClockFeaturesKHR rt_clock_features;
static qboolean rt_shader_clock;
static VkPhysicalDevicePipelineExecutablePropertiesFeaturesKHR rt_executable_features;
static qboolean rt_pipeline_statistics;

static const char *rt_extensions[] = {
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_QUERY_EXTENSION_NAME
};

static qboolean has_extension(const VkExtensionProperties *extensions,
	uint32_t count, const char *name)
{
	uint32_t i;
	for (i = 0; i < count; ++i) {
		if (!strcmp(extensions[i].extensionName, name))
			return qtrue;
	}
	return qfalse;
}

qboolean vk_rt_configure_device(VkPhysicalDevice physical_device,
	const VkExtensionProperties *extensions, uint32_t extension_count,
	PFN_vkGetPhysicalDeviceFeatures2 get_features2, void **device_features)
{
	uint32_t i;
	VkPhysicalDeviceFeatures2 features;
	VkPhysicalDeviceProperties properties;

	rt.device_supported = qfalse;
	rt.path_supported = qfalse;
	rt_shader_clock = qfalse;
	rt_pipeline_statistics = qfalse;
	memset(&rt_executable_features, 0, sizeof(rt_executable_features));
	if (device_features)
		*device_features = NULL;
	if (!get_features2)
		return qfalse;
	for (i = 0; i < sizeof(rt_extensions) / sizeof(rt_extensions[0]); ++i) {
		if (!has_extension(extensions, extension_count, rt_extensions[i]))
			return qfalse;
	}

	memset(&features, 0, sizeof(features));
	memset(&rt_bda_features, 0, sizeof(rt_bda_features));
	memset(&rt_as_features, 0, sizeof(rt_as_features));
	memset(&rt_query_features, 0, sizeof(rt_query_features));
	memset(&rt_texture_features, 0, sizeof(rt_texture_features));
	rt_texture_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	rt_bda_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	rt_as_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	rt_query_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	features.pNext = &rt_bda_features;
	rt_bda_features.pNext = &rt_as_features;
	rt_as_features.pNext = &rt_query_features;
	rt_query_features.pNext = &rt_texture_features;
	memset(&rt_clock_features, 0, sizeof(rt_clock_features));
	if (has_extension(extensions, extension_count, VK_KHR_SHADER_CLOCK_EXTENSION_NAME)) {
		rt_clock_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
		rt_texture_features.pNext = &rt_clock_features;
	}
	get_features2(physical_device, &features);
	qvkGetPhysicalDeviceProperties(physical_device, &properties);
	rt.path_supported = rt_texture_features.shaderSampledImageArrayNonUniformIndexing &&
		properties.limits.maxPerStageDescriptorSamplers >= PT_MAX_TEXTURES + 2 &&
		properties.limits.maxPerStageDescriptorSampledImages >= PT_MAX_TEXTURES + 2 &&
		properties.limits.maxDescriptorSetSamplers >= PT_MAX_TEXTURES + 2 &&
		properties.limits.maxDescriptorSetSampledImages >= PT_MAX_TEXTURES + 2 &&
		properties.limits.maxPerStageDescriptorStorageBuffers >= 41 &&
		properties.limits.maxDescriptorSetStorageBuffers >= 41 &&
		properties.limits.maxPerStageDescriptorStorageImages >= 3 &&
		properties.limits.maxDescriptorSetStorageImages >= 3 &&
		properties.limits.maxPerStageResources >= PT_MAX_TEXTURES + 47;
	if (!rt_bda_features.bufferDeviceAddress ||
		!rt_as_features.accelerationStructure || !rt_query_features.rayQuery)
		return qfalse;

	/* Request the ray capabilities and, when supported, nonuniform material textures. */
	/* Diagnostics are optional and need one extra storage descriptor/set. */
	rt_shader_clock = rt.path_supported && rt_clock_features.shaderSubgroupClock &&
		properties.limits.maxPerStageDescriptorStorageBuffers >= 42 &&
		properties.limits.maxDescriptorSetStorageBuffers >= 42 &&
		properties.limits.maxBoundDescriptorSets >= 2 &&
		properties.limits.maxPerStageResources >= PT_MAX_TEXTURES + 48;
	memset(&rt_clock_features, 0, sizeof(rt_clock_features));
	memset(&rt_bda_features, 0, sizeof(rt_bda_features));
	memset(&rt_as_features, 0, sizeof(rt_as_features));
	memset(&rt_query_features, 0, sizeof(rt_query_features));
	rt_bda_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	rt_bda_features.bufferDeviceAddress = VK_TRUE;
	rt_as_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
	rt_as_features.accelerationStructure = VK_TRUE;
	rt_query_features.sType =
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
	rt_query_features.rayQuery = VK_TRUE;
	rt_bda_features.pNext = &rt_as_features;
	rt_as_features.pNext = &rt_query_features;
	rt_query_features.pNext = NULL;
	memset(&rt_texture_features, 0, sizeof(rt_texture_features));
	if (rt.path_supported) {
		rt_texture_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
		rt_texture_features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
		rt_query_features.pNext = &rt_texture_features;
		if (rt_shader_clock) {
			rt_clock_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
			rt_clock_features.shaderSubgroupClock = VK_TRUE;
			rt_texture_features.pNext = &rt_clock_features;
		}
	}
	if (has_extension(extensions, extension_count, VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME)) {
		VkPhysicalDeviceFeatures2 executable_query = { .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		rt_executable_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_EXECUTABLE_PROPERTIES_FEATURES_KHR;
		executable_query.pNext = &rt_executable_features;
		get_features2(physical_device, &executable_query);
		rt_pipeline_statistics = rt_executable_features.pipelineExecutableInfo != 0;
	}
	if (rt_pipeline_statistics) rt_executable_features.pNext = &rt_bda_features;
	if (device_features)
		*device_features = rt_pipeline_statistics ? (void *)&rt_executable_features : (void *)&rt_bda_features;
	rt.device_supported = qtrue;
	return qtrue;
}

uint32_t vk_rt_device_extension_count(void)
{
	return rt.device_supported ?
		(uint32_t)(sizeof(rt_extensions) / sizeof(rt_extensions[0])) + (rt_shader_clock ? 1 : 0) + (rt_pipeline_statistics ? 1 : 0) : 0;
}

const char *vk_rt_device_extension(uint32_t index)
{
	if (index >= vk_rt_device_extension_count()) return NULL;
	if (index < ARRAY_LEN(rt_extensions)) return rt_extensions[index];
	index -= ARRAY_LEN(rt_extensions);
	if (rt_shader_clock && index == 0) return VK_KHR_SHADER_CLOCK_EXTENSION_NAME;
	return VK_KHR_PIPELINE_EXECUTABLE_PROPERTIES_EXTENSION_NAME;
}

qboolean vk_rt_pipeline_statistics_supported(void)
{
	return rt_pipeline_statistics;
}

qboolean vk_rt_shader_clock_supported(void)
{
	return rt_shader_clock;
}

qboolean vk_rt_supported(void)
{
	return rt.device_supported;
}

static qboolean create_buffer(rt_buffer_t *target, VkDeviceSize size,
	VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_flags, qboolean map)
{
	VkBufferCreateInfo info;
	VkMemoryRequirements requirements;
	VkMemoryAllocateFlagsInfo flags;
	VkMemoryAllocateInfo allocation;
	VkBufferDeviceAddressInfo address_info;

	memset(target, 0, sizeof(*target));
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = usage | (map ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if (qvkCreateBuffer(vk.device, &info, NULL, &target->buffer) != VK_SUCCESS)
		return qfalse;

	qvkGetBufferMemoryRequirements(vk.device, target->buffer, &requirements);
	memset(&flags, 0, sizeof(flags));
	flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
	flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	memset(&allocation, 0, sizeof(allocation));
	allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocation.pNext = &flags;
	allocation.allocationSize = requirements.size;
	allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
		map ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : memory_flags);
	if (qvkAllocateMemory(vk.device, &allocation, NULL, &target->memory) != VK_SUCCESS)
		return qfalse;
	if (qvkBindBufferMemory(vk.device, target->buffer, target->memory, 0) != VK_SUCCESS)
		return qfalse;
	if (map) {
		target->mapped = malloc((size_t)size);
		if (!target->mapped) return qfalse;
		info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		if (qvkCreateBuffer(vk.device, &info, NULL, &target->upload) != VK_SUCCESS) return qfalse;
		qvkGetBufferMemoryRequirements(vk.device, target->upload, &requirements);
		allocation.pNext = NULL;
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex = find_memory_type(requirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		if (qvkAllocateMemory(vk.device, &allocation, NULL, &target->upload_memory) != VK_SUCCESS ||
			qvkBindBufferMemory(vk.device, target->upload, target->upload_memory, 0) != VK_SUCCESS ||
			qvkMapMemory(vk.device, target->upload_memory, 0, size, 0, &target->upload_mapped) != VK_SUCCESS)
			return qfalse;
	}

	memset(&address_info, 0, sizeof(address_info));
	address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	address_info.buffer = target->buffer;
	target->address = rt.get_buffer_address(vk.device, &address_info);
	target->size = size;
	return target->address != 0;
}

static void free_world_geometry(void)
{
	if (rt.world_vertices)
		ri.Free(rt.world_vertices);
	if (rt.world_indices)
		ri.Free(rt.world_indices);
	rt.world_vertices = NULL;
	rt.world_indices = NULL;
	rt.world_vertex_count = 0;
	rt.world_index_count = 0;
	rt.world_built = qfalse;
	rt.world_upload_pending = qtrue;
	rt.world_ray_distance = 8192.0f;
}

#include "pt_world_mirror.h"

static qboolean world_surface_counts(const msurface_t *surface,
	uint32_t *vertices, uint32_t *indices)
{
	if (!surface || !surface->data || !surface->shader ||
		surface->shader->nvOverlay ||
		(r_rayTracing->integer == 2 ? ((surface->shader->sort == SS_PORTAL && !world_surface_is_mirror(surface)) ||
            surface->shader->sort == SS_FOG || surface->shader->sort == SS_STENCIL_SHADOW) :
		(surface->shader->sort > SS_OPAQUE || surface->shader->isSky)))
		return qfalse;
	/* Deformed world surfaces are tessellated every frame, including off-screen
	 * ones, instead of inserting their undeformed mesh into the static scene. */
	if (r_rayTracing->integer == 2 && surface->shader->numDeforms)
		return qfalse;
	switch (*surface->data) {
	case SF_FACE: {
		const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surface->data;
		*vertices = (uint32_t)face->numPoints;
		*indices = (uint32_t)face->numIndices;
		return qtrue;
	}
	case SF_GRID: {
		const srfGridMesh_t *grid = (const srfGridMesh_t *)surface->data;
		if (grid->width < 2 || grid->height < 2)
			return qfalse;
		*vertices = (uint32_t)(grid->width * grid->height);
		*indices = (uint32_t)((grid->width - 1) * (grid->height - 1) * 6);
		return qtrue;
	}
	case SF_TRIANGLES: {
		const srfTriangles_t *triangles = (const srfTriangles_t *)surface->data;
		*vertices = (uint32_t)triangles->numVerts;
		*indices = (uint32_t)triangles->numIndexes;
		return qtrue;
	}
	default:
		return qfalse;
	}
}

void vk_rt_load_world(void)
{
	const bmodel_t *world_model;
	uint32_t vertex_count = 0;
	uint32_t index_count = 0;
	uint32_t vertex_cursor = 0;
	uint32_t index_cursor = 0;
	uint32_t mirror_surfaces = 0;
	int i;

	free_world_geometry();
	if (!rt.initialized || !tr.world || !tr.world->bmodels)
		return;
	rt.path_frozen = qfalse;
	world_model = &tr.world->bmodels[0];
	for (i = 0; i < world_model->numSurfaces; ++i) {
		uint32_t vertices, indices;
		if (!world_surface_counts(&world_model->firstSurface[i],
			&vertices, &indices))
			continue;
		if (vertex_count + vertices > RT_MAX_VERTICES ||
			index_count + indices > RT_MAX_INDICES) {
			if (r_rayTracing->integer == 2)
				ri.Error(ERR_DROP, "Path tracing: world geometry capacity exceeded");
			ri.Printf(PRINT_WARNING,
				"RTX full-world geometry exceeds acceleration-structure capacity\n");
			return;
		}
		vertex_count += vertices;
		index_count += indices;
	}
	if (!vertex_count || !index_count)
		return;
	qboolean material_test = r_rayTracing->integer == 2 && r_pathTracingTestScene->integer;
	if (material_test) {
		if (vertex_count + PT_TEST_VERTICES > RT_MAX_VERTICES || index_count + PT_TEST_INDICES > RT_MAX_INDICES)
			ri.Error(ERR_DROP, "Path tracing: no room for material test scene");
		vertex_count += PT_TEST_VERTICES;
		index_count += PT_TEST_INDICES;
	}
	rt.world_vertices = (float *)ri.Malloc((int)(vertex_count * 3 * sizeof(float)));
	rt.world_indices = (uint32_t *)ri.Malloc((int)(index_count * sizeof(uint32_t)));
	if (!rt.world_vertices || !rt.world_indices) {
		free_world_geometry();
		return;
	}
	vk_pt_begin_world(vertex_count, index_count);

	for (i = 0; i < world_model->numSurfaces; ++i) {
		const msurface_t *surface = &world_model->firstSurface[i];
		uint32_t vertices, indices, j;
		uint32_t base = vertex_cursor;
		if (!world_surface_counts(surface, &vertices, &indices))
			continue;
		vk_pt_world_surface(index_cursor, indices, surface->shader);
		if (surface->shader->sort == SS_PORTAL) ++mirror_surfaces;
		switch (*surface->data) {
		case SF_FACE: {
			const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surface->data;
			const uint32_t *source_indices = (const uint32_t *)
				((const byte *)face + face->ofsIndices);
			for (j = 0; j < vertices; ++j) {
				memcpy(&rt.world_vertices[(vertex_cursor + j) * 3],
					face->points[j], sizeof(float) * 3);
				vk_pt_world_vertex(vertex_cursor + j, face->plane.normal, &face->points[j][3],
					((const byte *)&face->points[j][7])[3]);
			}
			for (j = 0; j < indices; ++j)
				rt.world_indices[index_cursor + j] = base + source_indices[j];
			break;
		}
		case SF_GRID: {
			const srfGridMesh_t *grid = (const srfGridMesh_t *)surface->data;
			int y, x;
			for (j = 0; j < vertices; ++j) {
				memcpy(&rt.world_vertices[(vertex_cursor + j) * 3],
					grid->verts[j].xyz, sizeof(float) * 3);
				vk_pt_world_vertex(vertex_cursor + j, grid->verts[j].normal, grid->verts[j].st,
					grid->verts[j].color[3]);
			}
			for (y = 0; y < grid->height - 1; ++y) {
				for (x = 0; x < grid->width - 1; ++x) {
					uint32_t v2 = base + (uint32_t)(y * grid->width + x);
					uint32_t v1 = v2 + 1;
					uint32_t v3 = v2 + (uint32_t)grid->width;
					uint32_t v4 = v3 + 1;
					rt.world_indices[index_cursor++] = v2;
					rt.world_indices[index_cursor++] = v3;
					rt.world_indices[index_cursor++] = v1;
					rt.world_indices[index_cursor++] = v1;
					rt.world_indices[index_cursor++] = v3;
					rt.world_indices[index_cursor++] = v4;
				}
			}
			indices = 0;
			break;
		}
		case SF_TRIANGLES: {
			const srfTriangles_t *triangles = (const srfTriangles_t *)surface->data;
			for (j = 0; j < vertices; ++j) {
				memcpy(&rt.world_vertices[(vertex_cursor + j) * 3],
					triangles->verts[j].xyz, sizeof(float) * 3);
				vk_pt_world_vertex(vertex_cursor + j, triangles->verts[j].normal, triangles->verts[j].st,
					triangles->verts[j].color[3]);
			}
			for (j = 0; j < indices; ++j)
				rt.world_indices[index_cursor + j] = base +
					(uint32_t)triangles->indexes[j];
			break;
		}
		default:
			break;
		}
		vertex_cursor += vertices;
		index_cursor += indices;
	}
	if (r_rayTracing->integer == 2)
		ri.Printf(PRINT_ALL, "Path tracing: %u static BSP mirror surfaces loaded\n", mirror_surfaces);
	if (material_test) vk_pt_test_scene(rt.world_vertices, rt.world_indices, &vertex_cursor, &index_cursor);
	if (r_rayTracing->integer == 2)
		rt.world_opaque_indices = vk_pt_partition_world(rt.world_indices, index_cursor);
	rt.world_vertex_count = vertex_cursor;
	rt.world_index_count = index_cursor;
	{
		vec3_t mins, maxs, diagonal;
		uint32_t vertex;
		ClearBounds(mins, maxs);
		for (vertex = 0; vertex < vertex_cursor; ++vertex)
			AddPointToBounds(&rt.world_vertices[vertex * 3], mins, maxs);
		VectorSubtract(maxs, mins, diagonal);
		/* Shadow reach is a property of the map, not the camera's visible
		 * bounds. A changing raster far plane must not clip shadow casters. */
		rt.world_ray_distance = fmaxf(8192.0f, VectorLength(diagonal));
	}
	ri.Printf(PRINT_ALL, "NVIDIA RTX world geometry: %u triangles loaded\n",
		rt.world_index_count / 3);
}

static void destroy_buffer(rt_buffer_t *target)
{
	free(target->mapped);
	if (target->upload_mapped) qvkUnmapMemory(vk.device, target->upload_memory);
	if (target->upload) qvkDestroyBuffer(vk.device, target->upload, NULL);
	if (target->upload_memory) qvkFreeMemory(vk.device, target->upload_memory, NULL);
	if (target->buffer)
		qvkDestroyBuffer(vk.device, target->buffer, NULL);
	if (target->memory)
		qvkFreeMemory(vk.device, target->memory, NULL);
	memset(target, 0, sizeof(*target));
}

static void destroy_as(rt_acceleration_structure_t *target)
{
	if (target->handle)
		rt.destroy_as(vk.device, target->handle, NULL);
	destroy_buffer(&target->storage);
	memset(target, 0, sizeof(*target));
}

static qboolean ensure_as(rt_acceleration_structure_t *target,
	VkDeviceSize required_size, VkAccelerationStructureTypeKHR type)
{
	VkAccelerationStructureCreateInfoKHR info;
	VkDeviceSize capacity;

	if (target->handle && target->capacity >= required_size)
		return qtrue;
	destroy_as(target);
	capacity = (required_size + 65535u) & ~((VkDeviceSize)65535u);
	if (!create_buffer(&target->storage, capacity,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, qfalse))
		return qfalse;
	memset(&info, 0, sizeof(info));
	info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	info.buffer = target->storage.buffer;
	info.size = capacity;
	info.type = type;
	if (rt.create_as(vk.device, &info, NULL, &target->handle) != VK_SUCCESS)
		return qfalse;
	target->capacity = capacity;
	return qtrue;
}

static qboolean ensure_scratch(VkDeviceSize required_size)
{
	VkDeviceSize capacity;
	if (rt.scratch.buffer && rt.scratch.size >= required_size)
		return qtrue;
	destroy_buffer(&rt.scratch);
	capacity = (required_size + 65535u) & ~((VkDeviceSize)65535u);
	return create_buffer(&rt.scratch, capacity,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, qfalse);
}

static void update_image_descriptors(VkImageView color_view,
	VkImageView depth_view, VkImageView output_view)
{
	VkDescriptorImageInfo images[3];
	VkWriteDescriptorSet writes[3];
	memset(images, 0, sizeof(images));
	memset(writes, 0, sizeof(writes));
	images[0].sampler = rt.sampler;
	images[0].imageView = color_view;
	images[0].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	images[1].sampler = rt.sampler;
	images[1].imageView = depth_view;
	images[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	images[2].imageView = output_view;
	images[2].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	for (uint32_t i = 0; i < 3; ++i) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = rt.descriptor_set;
		writes[i].dstBinding = i + 1;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = i < 2 ?
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
			VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[i].pImageInfo = &images[i];
	}
	qvkUpdateDescriptorSets(vk.device, 3, writes, 0, NULL);
}

qboolean vk_rt_initialize(uint32_t width, uint32_t height,
	VkImageView color_view, VkFormat color_format,
	VkImageView depth_view, VkFormat depth_format,
	VkImageView output_view, VkImageView motion_view, VkImageView path_depth_view)
{
	VkDescriptorSetLayoutBinding bindings[4];
	VkDescriptorSetLayoutCreateInfo set_info;
	VkDescriptorPoolSize pool_sizes[3];
	VkDescriptorPoolCreateInfo pool_info;
	VkDescriptorSetAllocateInfo set_allocation;
	VkPushConstantRange push_range;
	VkPipelineLayoutCreateInfo layout_info;
	VkShaderModule shader_module;
	VkShaderModuleCreateInfo shader_info;
	VkComputePipelineCreateInfo pipeline_info;
	VkSamplerCreateInfo sampler_info;
	extern unsigned char rt_shadows_comp_spv[];
	extern int rt_shadows_comp_spv_size;

	(void)color_format;
	(void)depth_format;
	ri.Cvar_Set("r_rayTracingAvailable", rt.device_supported ? "1" : "0");
	if (!rt.device_supported || !r_rayTracing->integer)
		return qfalse;
	if (r_rayTracing->integer == 2 && !rt.path_supported) {
		ri.Printf(PRINT_WARNING, "Path tracing requires nonuniform sampled-image indexing and 514 texture descriptors\n");
		return qfalse;
	}

	rt.get_buffer_address = (PFN_vkGetBufferDeviceAddress)
		qvkGetDeviceProcAddr(vk.device, "vkGetBufferDeviceAddress");
	if (!rt.get_buffer_address)
		rt.get_buffer_address = (PFN_vkGetBufferDeviceAddress)
			qvkGetDeviceProcAddr(vk.device, "vkGetBufferDeviceAddressKHR");
	rt.create_as = (PFN_vkCreateAccelerationStructureKHR)
		qvkGetDeviceProcAddr(vk.device, "vkCreateAccelerationStructureKHR");
	rt.destroy_as = (PFN_vkDestroyAccelerationStructureKHR)
		qvkGetDeviceProcAddr(vk.device, "vkDestroyAccelerationStructureKHR");
	rt.get_build_sizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)
		qvkGetDeviceProcAddr(vk.device, "vkGetAccelerationStructureBuildSizesKHR");
	rt.cmd_build_as = (PFN_vkCmdBuildAccelerationStructuresKHR)
		qvkGetDeviceProcAddr(vk.device, "vkCmdBuildAccelerationStructuresKHR");
	rt.get_as_address = (PFN_vkGetAccelerationStructureDeviceAddressKHR)
		qvkGetDeviceProcAddr(vk.device, "vkGetAccelerationStructureDeviceAddressKHR");
	if (!rt.get_buffer_address || !rt.create_as || !rt.destroy_as ||
		!rt.get_build_sizes || !rt.cmd_build_as || !rt.get_as_address)
		goto fail;

	if (!create_buffer(&rt.vertices, RT_MAX_VERTICES * sizeof(float) * 3,
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue) ||
		!create_buffer(&rt.indices, RT_MAX_INDICES * sizeof(uint32_t),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue) ||
		!create_buffer(&rt.instances, 5 * sizeof(VkAccelerationStructureInstanceKHR),
		VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		qtrue))
		goto fail;

	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	sampler_info.magFilter = VK_FILTER_NEAREST;
	sampler_info.minFilter = VK_FILTER_NEAREST;
	sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	sampler_info.maxLod = 0.0f;
	if (qvkCreateSampler(vk.device, &sampler_info, NULL, &rt.sampler) != VK_SUCCESS)
		goto fail;

	memset(bindings, 0, sizeof(bindings));
	for (uint32_t i = 0; i < 4; ++i) {
		bindings[i].binding = i;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	memset(&set_info, 0, sizeof(set_info));
	set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	set_info.bindingCount = 4;
	set_info.pBindings = bindings;
	if (qvkCreateDescriptorSetLayout(vk.device, &set_info, NULL,
		&rt.set_layout) != VK_SUCCESS)
		goto fail;

	memset(pool_sizes, 0, sizeof(pool_sizes));
	pool_sizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	pool_sizes[0].descriptorCount = 1;
	pool_sizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	pool_sizes[1].descriptorCount = 2;
	pool_sizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	pool_sizes[2].descriptorCount = 1;
	memset(&pool_info, 0, sizeof(pool_info));
	pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	pool_info.maxSets = 1;
	pool_info.poolSizeCount = 3;
	pool_info.pPoolSizes = pool_sizes;
	if (qvkCreateDescriptorPool(vk.device, &pool_info, NULL,
		&rt.descriptor_pool) != VK_SUCCESS)
		goto fail;
	memset(&set_allocation, 0, sizeof(set_allocation));
	set_allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	set_allocation.descriptorPool = rt.descriptor_pool;
	set_allocation.descriptorSetCount = 1;
	set_allocation.pSetLayouts = &rt.set_layout;
	if (qvkAllocateDescriptorSets(vk.device, &set_allocation,
		&rt.descriptor_set) != VK_SUCCESS)
		goto fail;

	memset(&push_range, 0, sizeof(push_range));
	push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	push_range.size = sizeof(float) * 24;
	memset(&layout_info, 0, sizeof(layout_info));
	layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layout_info.setLayoutCount = 1;
	layout_info.pSetLayouts = &rt.set_layout;
	layout_info.pushConstantRangeCount = 1;
	layout_info.pPushConstantRanges = &push_range;
	if (qvkCreatePipelineLayout(vk.device, &layout_info, NULL,
		&rt.pipeline_layout) != VK_SUCCESS)
		goto fail;

	memset(&shader_info, 0, sizeof(shader_info));
	shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	shader_info.codeSize = (size_t)rt_shadows_comp_spv_size;
	shader_info.pCode = (const uint32_t *)rt_shadows_comp_spv;
	if (qvkCreateShaderModule(vk.device, &shader_info, NULL,
		&shader_module) != VK_SUCCESS)
		goto fail;
	memset(&pipeline_info, 0, sizeof(pipeline_info));
	pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	pipeline_info.stage.module = shader_module;
	pipeline_info.stage.pName = "main";
	pipeline_info.layout = rt.pipeline_layout;
	if (qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline_info,
		NULL, &rt.pipeline) != VK_SUCCESS) {
		qvkDestroyShaderModule(vk.device, shader_module, NULL);
		goto fail;
	}
	qvkDestroyShaderModule(vk.device, shader_module, NULL);
	update_image_descriptors(color_view, depth_view, output_view);
	rt.width = width;
	rt.height = height;
	if (r_rayTracing->integer == 2 && !vk_pt_initialize(width, height,
		RT_MAX_VERTICES, RT_MAX_INDICES, color_view, depth_view, output_view,
        motion_view, path_depth_view))
		goto fail;
	rt.initialized = qtrue;
	ri.Printf(PRINT_ALL, "Native ray-query %s initialized at %ux%u\n",
		r_rayTracing->integer == 2 ? "path tracing" : "shadows", width, height);
	return qtrue;

fail:
	ri.Printf(PRINT_WARNING,
		"NVIDIA RTX ray-query initialization failed; raster rendering remains active\n");
	vk_rt_shutdown();
	return qfalse;
}

void vk_rt_shutdown(void)
{
	vk_pt_shutdown();
	if (!vk.device) {
		free_world_geometry();
		memset(&rt, 0, sizeof(rt));
		return;
	}
	destroy_as(&rt.tlas);
	destroy_as(&rt.blas);
	destroy_as(&rt.world_blas);
	destroy_as(&rt.weapon_blas);
	destroy_as(&rt.opaque_blas);
	destroy_as(&rt.opaque_weapon_blas);
	destroy_buffer(&rt.scratch);
	destroy_buffer(&rt.instances);
	destroy_buffer(&rt.indices);
	destroy_buffer(&rt.vertices);
	if (rt.pipeline)
		qvkDestroyPipeline(vk.device, rt.pipeline, NULL);
	if (rt.pipeline_layout)
		qvkDestroyPipelineLayout(vk.device, rt.pipeline_layout, NULL);
	if (rt.descriptor_pool)
		qvkDestroyDescriptorPool(vk.device, rt.descriptor_pool, NULL);
	if (rt.set_layout)
		qvkDestroyDescriptorSetLayout(vk.device, rt.set_layout, NULL);
	if (rt.sampler)
		qvkDestroySampler(vk.device, rt.sampler, NULL);
	{
		qboolean supported = rt.device_supported;
		qboolean path_supported = rt.path_supported;
		free_world_geometry();
		memset(&rt, 0, sizeof(rt));
		rt.device_supported = supported;
		rt.path_supported = path_supported;
	}
}

void vk_rt_begin_frame(void)
{
	if (!rt.initialized)
		return;
	if (r_rayTracing->integer == 2 && r_pathTracingReference->integer && rt.path_frozen)
		return;
	rt.path_frozen = qfalse;
	rt.vertex_count = rt.world_vertex_count;
	rt.index_count = rt.world_index_count;
	if (rt.world_upload_pending && rt.world_vertex_count)
		memcpy(rt.vertices.mapped, rt.world_vertices,
			rt.world_vertex_count * 3 * sizeof(float));
	if (rt.world_upload_pending && rt.world_index_count)
		memcpy(rt.indices.mapped, rt.world_indices,
			rt.world_index_count * sizeof(uint32_t));
	rt.overflow_warned = qfalse;
	vk_pt_begin_frame();
	if (r_rayTracing->integer == 2 && r_pathTracingTestScene->integer == 2 &&
		rt.vertex_count + 4 <= RT_MAX_VERTICES && rt.index_count + 6 <= RT_MAX_INDICES)
		vk_pt_test_motion(rt.vertices.mapped, rt.indices.mapped, &rt.vertex_count, &rt.index_count);
}

void vk_rt_capture_geometry(const float (*vertices)[4], uint32_t vertex_count,
	const uint32_t *indices, uint32_t index_count, const float *origin,
	const float (*axis)[3], qboolean opaque, const float (*normals)[4],
	const float (*uv)[2][2], shader_t *shader)
{
	float *dst_vertices;
	uint32_t *dst_indices;
	uint32_t base;
	uint32_t i;
	if (!rt.initialized || !opaque || !vertices || !indices ||
		index_count < 3 || vertex_count == 0)
		return;
	if (r_rayTracing->integer == 2 && r_pathTracingReference->integer && rt.path_frozen)
		return;
	if (rt.vertex_count + vertex_count > RT_MAX_VERTICES ||
		rt.index_count + index_count > RT_MAX_INDICES) {
		if (r_rayTracing->integer == 2)
			ri.Error(ERR_DROP, "Path tracing: dynamic geometry capacity exceeded");
		if (!rt.overflow_warned) {
			ri.Printf(PRINT_WARNING,
				"RTX geometry capacity exceeded; extra surfaces will not cast shadows\n");
			rt.overflow_warned = qtrue;
		}
		return;
	}
	base = rt.vertex_count;
	dst_vertices = (float *)rt.vertices.mapped + (size_t)base * 3;
	for (i = 0; i < vertex_count; ++i) {
		const float x = vertices[i][0];
		const float y = vertices[i][1];
		const float z = vertices[i][2];
		dst_vertices[i * 3 + 0] = x * axis[0][0] + y * axis[1][0] +
			z * axis[2][0] + origin[0];
		dst_vertices[i * 3 + 1] = x * axis[0][1] + y * axis[1][1] +
			z * axis[2][1] + origin[1];
		dst_vertices[i * 3 + 2] = x * axis[0][2] + y * axis[1][2] +
			z * axis[2][2] + origin[2];
	}
	dst_indices = (uint32_t *)rt.indices.mapped + rt.index_count;
	for (i = 0; i < index_count; ++i)
		dst_indices[i] = base + indices[i];
	vk_pt_capture(base, rt.index_count, vertex_count, index_count, normals, uv, axis, shader,
        dst_vertices, indices);
	rt.vertex_count += vertex_count;
	rt.index_count += index_count;
}

VkDeviceSize vk_rt_scene_upload_bytes(void)
{
	return rt.scene_upload_bytes;
}

static void upload_scene(VkCommandBuffer cmd, uint32_t instance_count)
{
	rt_buffer_t *buffers[3] = {&rt.vertices, &rt.indices, &rt.instances};
	VkDeviceSize sizes[3] = {rt.vertex_count*3*sizeof(float), rt.index_count*sizeof(uint32_t),
		instance_count*sizeof(VkAccelerationStructureInstanceKHR)};
	/* The completed-frame fence protects these persistent staging buffers.
	 * Map replacement invalidates the immutable prefix, independently of BLAS
	 * build state. Dynamic/weapon indices are repartitioned and sent every frame. */
	VkDeviceSize offsets[3] = {
		rt.world_upload_pending ? 0 : rt.world_vertex_count*3*sizeof(float),
		rt.world_upload_pending ? 0 : rt.world_index_count*sizeof(uint32_t), 0};
	VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL);
	rt.scene_upload_bytes = 0;
	for (uint32_t i = 0; i < 3; ++i) {
		if (offsets[i] > sizes[i] || sizes[i] > buffers[i]->size) {
			ri.Error(ERR_DROP, "RTX scene upload range exceeds captured geometry");
			return;
		}
		VkBufferCopy copy = {offsets[i], offsets[i], sizes[i]-offsets[i]};
		if (!copy.size) continue;
		memcpy((byte *)buffers[i]->upload_mapped+copy.srcOffset,
			(byte *)buffers[i]->mapped+copy.srcOffset, (size_t)copy.size);
		qvkCmdCopyBuffer(cmd, buffers[i]->upload, buffers[i]->buffer, 1, &copy);
		rt.scene_upload_bytes += copy.size;
	}
	rt.world_upload_pending = qfalse;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &barrier, 0, NULL, 0, NULL);
}

/* Static world geometry separates opaque and callback triangles. The optional
 * dynamic partition does the same through separate BLAS instances, preserving
 * existing shader addressing and first-person/decal visibility masks. */
static qboolean record_path_scene(VkCommandBuffer command_buffer,
	const float *projection, const float *camera_origin,
	const float *camera_axis, const float *sun_direction,
	float near_distance, float far_distance, qboolean reset_history)
{
	VkAccelerationStructureGeometryKHR geometry[5][2] = {0}, tlas_geometry = {0};
	VkAccelerationStructureBuildGeometryInfoKHR builds[5] = {0}, tlas_info = {0};
	VkAccelerationStructureBuildRangeInfoKHR ranges[5][2] = {0}, tlas_range = {0};
	VkAccelerationStructureBuildSizesInfoKHR sizes = {0};
	VkAccelerationStructureInstanceKHR instances[5] = {0};
	VkAccelerationStructureDeviceAddressInfoKHR address = {0};
	rt_acceleration_structure_t *targets[5] = {&rt.world_blas, &rt.opaque_blas,
		&rt.blas, &rt.opaque_weapon_blas, &rt.weapon_blas};
	VkDeviceSize scratch_size = 0;
	VkMemoryBarrier barrier = {0};
	uint32_t starts[5], counts[5];
	const uint32_t masks[5] = {7, 7, 23, 8, 8}; // Only callback dynamics contain decals (bit 16).
	vk_pt_partition_dynamic(rt.indices.mapped, rt.index_count, starts);
	for (uint32_t i = 0; i < 5; ++i)
		counts[i] = (i+1 < 5 ? starts[i+1] : rt.index_count)-starts[i];
	if (rt.dynamic_opaque_logged != 1+r_pathTracingDynamicOpaque->integer) {
		ri.Printf(PRINT_ALL, "PT_DYNAMIC_OPAQUE enabled=%d regular=%u weapon=%u callback=%u\n",
			r_pathTracingDynamicOpaque->integer, counts[1]/3, counts[3]/3, (counts[2]+counts[4])/3);
		rt.dynamic_opaque_logged = 1+r_pathTracingDynamicOpaque->integer;
	}
	uint32_t instance_count = 0;
	qboolean result;
	vk_pt_profile(command_buffer, 1);
	/* Size every allocation first: no recorded build may refer to scratch or
	 * acceleration storage that a later allocation grows and destroys. */
	for (uint32_t i = 0; i < 5; ++i) {
		uint32_t primitive_counts[2] = {counts[i]/3, 0};
		uint32_t geometry_count = i == 0 ? 2 : 1;
		if (!counts[i]) continue;
		if (i == 0) {
			primitive_counts[0] = rt.world_opaque_indices/3;
			primitive_counts[1] = (counts[0]-rt.world_opaque_indices)/3;
		}
		for (uint32_t j = 0; j < geometry_count; ++j) {
			VkAccelerationStructureGeometryTrianglesDataKHR *tri = &geometry[i][j].geometry.triangles;
			geometry[i][j].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
			geometry[i][j].geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
			geometry[i][j].flags = ((i == 0 && j == 0) || i == 1 || i == 3) ? VK_GEOMETRY_OPAQUE_BIT_KHR :
				VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
			tri->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
			tri->vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
			tri->vertexData.deviceAddress = rt.vertices.address;
			tri->vertexStride = 3*sizeof(float);
			tri->maxVertex = (i == 0 ? rt.world_vertex_count : rt.vertex_count)-1;
			tri->indexType = VK_INDEX_TYPE_UINT32;
			tri->indexData.deviceAddress = rt.indices.address +
				(VkDeviceSize)(starts[i]+(j ? rt.world_opaque_indices : 0))*sizeof(uint32_t);
			ranges[i][j].primitiveCount = primitive_counts[j];
		}
		builds[i].sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
		builds[i].type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		builds[i].flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		builds[i].mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		builds[i].geometryCount = geometry_count;
		builds[i].pGeometries = geometry[i];
		if (i != 0 || !rt.world_built) {
			sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
			rt.get_build_sizes(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
				&builds[i], primitive_counts, &sizes);
			if (!ensure_as(targets[i], sizes.accelerationStructureSize,
				VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)) return qfalse;
			if (sizes.buildScratchSize > scratch_size) scratch_size = sizes.buildScratchSize;
		}
		builds[i].dstAccelerationStructure = targets[i]->handle;
		address.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
		address.accelerationStructure = targets[i]->handle;
		instances[instance_count].transform.matrix[0][0] = 1;
		instances[instance_count].transform.matrix[1][1] = 1;
		instances[instance_count].transform.matrix[2][2] = 1;
		instances[instance_count].instanceCustomIndex = starts[i]/3;
		instances[instance_count].mask = masks[i];
		instances[instance_count].flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instances[instance_count].accelerationStructureReference = rt.get_as_address(vk.device, &address);
		++instance_count;
	}
	memcpy(rt.instances.mapped, instances, instance_count*sizeof(instances[0]));
	tlas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlas_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	tlas_geometry.geometry.instances.data.deviceAddress = rt.instances.address;
	tlas_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	tlas_info.geometryCount = 1;
	tlas_info.pGeometries = &tlas_geometry;
	sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	rt.get_build_sizes(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&tlas_info, &instance_count, &sizes);
	if (sizes.buildScratchSize > scratch_size) scratch_size = sizes.buildScratchSize;
	if (!ensure_as(&rt.tlas, sizes.accelerationStructureSize, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) ||
		!ensure_scratch(scratch_size)) return qfalse;
	upload_scene(command_buffer, instance_count);
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	for (uint32_t i = 0; i < 5; ++i) {
		const VkAccelerationStructureBuildRangeInfoKHR *range = ranges[i];
		if (!counts[i] || (i == 0 && rt.world_built)) continue;
		builds[i].scratchData.deviceAddress = rt.scratch.address;
		rt.cmd_build_as(command_buffer, 1, &builds[i], &range);
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
			VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		qvkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
			VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL);
	}
	rt.world_built = counts[0] != 0;
	tlas_info.dstAccelerationStructure = rt.tlas.handle;
	tlas_info.scratchData.deviceAddress = rt.scratch.address;
	tlas_range.primitiveCount = instance_count;
	{
		const VkAccelerationStructureBuildRangeInfoKHR *range = &tlas_range;
		rt.cmd_build_as(command_buffer, 1, &tlas_info, &range);
	}
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
	vk_pt_profile(command_buffer, 2);
	result = vk_pt_record(command_buffer, rt.tlas.handle, rt.vertices.buffer,
		rt.indices.buffer, rt.vertices.mapped, rt.indices.mapped, rt.vertex_count, rt.index_count,
		projection, camera_origin, camera_axis, sun_direction, near_distance, far_distance,
		rt.world_ray_distance, reset_history);
	rt.path_frozen = r_pathTracingReference->integer && result;
	return result;
}

qboolean vk_rt_record(VkCommandBuffer command_buffer,
	const float *projection, const float *camera_origin,
	const float *camera_axis, const float *sun_direction,
	float near_distance, float far_distance, qboolean reset_history)
{
	VkAccelerationStructureGeometryTrianglesDataKHR triangles;
	VkAccelerationStructureGeometryKHR blas_geometry;
	VkAccelerationStructureBuildGeometryInfoKHR blas_info;
	VkAccelerationStructureBuildSizesInfoKHR blas_sizes;
	VkAccelerationStructureBuildRangeInfoKHR blas_range;
	const VkAccelerationStructureBuildRangeInfoKHR *blas_ranges = &blas_range;
	VkAccelerationStructureDeviceAddressInfoKHR address_info;
	VkAccelerationStructureInstanceKHR instance;
	VkAccelerationStructureGeometryInstancesDataKHR instances_data;
	VkAccelerationStructureGeometryKHR tlas_geometry;
	VkAccelerationStructureBuildGeometryInfoKHR tlas_info;
	VkAccelerationStructureBuildSizesInfoKHR tlas_sizes;
	VkAccelerationStructureBuildRangeInfoKHR tlas_range;
	const VkAccelerationStructureBuildRangeInfoKHR *tlas_ranges = &tlas_range;
	VkMemoryBarrier barrier;
	VkWriteDescriptorSetAccelerationStructureKHR as_write;
	VkWriteDescriptorSet write;
	float constants[24];
	uint32_t primitive_count;

	if (!rt.initialized || rt.index_count < 3 || !projection ||
		!camera_origin || !camera_axis || !sun_direction)
		return qfalse;
	if (r_rayTracing->integer == 2)
		return record_path_scene(command_buffer, projection, camera_origin, camera_axis,
			sun_direction, near_distance, far_distance, reset_history);
	primitive_count = rt.index_count / 3;
	vk_pt_profile(command_buffer, 1);
	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	qvkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_HOST_BIT,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, NULL, 0, NULL);
	memset(&triangles, 0, sizeof(triangles));
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData.deviceAddress = rt.vertices.address;
	triangles.vertexStride = sizeof(float) * 3;
	triangles.maxVertex = rt.vertex_count - 1;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = rt.indices.address;
	memset(&blas_geometry, 0, sizeof(blas_geometry));
	blas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	blas_geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	blas_geometry.flags = r_rayTracing->integer == 2 ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;
	blas_geometry.geometry.triangles = triangles;
	memset(&blas_info, 0, sizeof(blas_info));
	blas_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	blas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	blas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	blas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	blas_info.geometryCount = 1;
	blas_info.pGeometries = &blas_geometry;
	memset(&blas_sizes, 0, sizeof(blas_sizes));
	blas_sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	rt.get_build_sizes(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&blas_info, &primitive_count, &blas_sizes);
	if (!ensure_as(&rt.blas, blas_sizes.accelerationStructureSize,
		VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR))
		return qfalse;
	blas_info.dstAccelerationStructure = rt.blas.handle;
	memset(&blas_range, 0, sizeof(blas_range));
	blas_range.primitiveCount = primitive_count;

	memset(&address_info, 0, sizeof(address_info));
	address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	address_info.accelerationStructure = rt.blas.handle;
	memset(&instance, 0, sizeof(instance));
	instance.transform.matrix[0][0] = 1.0f;
	instance.transform.matrix[1][1] = 1.0f;
	instance.transform.matrix[2][2] = 1.0f;
	instance.mask = 0xff;
	instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
	instance.accelerationStructureReference = rt.get_as_address(vk.device, &address_info);
	memcpy(rt.instances.mapped, &instance, sizeof(instance));

	memset(&instances_data, 0, sizeof(instances_data));
	instances_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instances_data.data.deviceAddress = rt.instances.address;
	memset(&tlas_geometry, 0, sizeof(tlas_geometry));
	tlas_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	tlas_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	tlas_geometry.geometry.instances = instances_data;
	memset(&tlas_info, 0, sizeof(tlas_info));
	tlas_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	tlas_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	tlas_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	tlas_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	tlas_info.geometryCount = 1;
	tlas_info.pGeometries = &tlas_geometry;
	primitive_count = 1;
	memset(&tlas_sizes, 0, sizeof(tlas_sizes));
	tlas_sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	rt.get_build_sizes(vk.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
		&tlas_info, &primitive_count, &tlas_sizes);
	if (!ensure_as(&rt.tlas, tlas_sizes.accelerationStructureSize,
		VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) ||
		!ensure_scratch(blas_sizes.buildScratchSize > tlas_sizes.buildScratchSize ?
			blas_sizes.buildScratchSize : tlas_sizes.buildScratchSize))
		return qfalse;

	/* Size shared scratch storage before recording either build. */
	upload_scene(command_buffer, 1);
	blas_info.scratchData.deviceAddress = rt.scratch.address;
	rt.cmd_build_as(command_buffer, 1, &blas_info, &blas_ranges);

	memset(&barrier, 0, sizeof(barrier));
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	/* The TLAS reads the BLAS and also writes the same scratch allocation. */
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
		VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	qvkCmdPipelineBarrier(command_buffer,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, NULL, 0, NULL);

	tlas_info.dstAccelerationStructure = rt.tlas.handle;
	tlas_info.scratchData.deviceAddress = rt.scratch.address;
	memset(&tlas_range, 0, sizeof(tlas_range));
	tlas_range.primitiveCount = 1;
	rt.cmd_build_as(command_buffer, 1, &tlas_info, &tlas_ranges);

	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
		VK_ACCESS_SHADER_READ_BIT;
	qvkCmdPipelineBarrier(command_buffer,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier,
		0, NULL, 0, NULL);

	memset(&as_write, 0, sizeof(as_write));
	as_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
	as_write.accelerationStructureCount = 1;
	as_write.pAccelerationStructures = &rt.tlas.handle;
	memset(&write, 0, sizeof(write));
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.pNext = &as_write;
	write.dstSet = rt.descriptor_set;
	write.dstBinding = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);

	constants[0] = camera_origin[0];
	constants[1] = camera_origin[1];
	constants[2] = camera_origin[2];
	constants[3] = near_distance;
	constants[4] = camera_axis[0];
	constants[5] = camera_axis[1];
	constants[6] = camera_axis[2];
	constants[7] = far_distance;
	constants[8] = -camera_axis[3];
	constants[9] = -camera_axis[4];
	constants[10] = -camera_axis[5];
	constants[11] = projection[0];
	constants[12] = camera_axis[6];
	constants[13] = camera_axis[7];
	constants[14] = camera_axis[8];
	constants[15] = projection[5];
	constants[16] = sun_direction[0];
	constants[17] = sun_direction[1];
	constants[18] = sun_direction[2];
	constants[19] = r_rayTracingShadowStrength->value;
	constants[20] = r_rayTracingShadowBias->value;
	constants[21] = rt.world_ray_distance;
	constants[22] = projection[8];
	constants[23] = projection[9];
	qvkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, rt.pipeline);
	qvkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		rt.pipeline_layout, 0, 1, &rt.descriptor_set, 0, NULL);
	qvkCmdPushConstants(command_buffer, rt.pipeline_layout,
		VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), constants);
	qvkCmdDispatch(command_buffer, (rt.width + 7) / 8, (rt.height + 7) / 8, 1);
	if (!rt.active_logged) {
		ri.Printf(PRINT_ALL,
			"NVIDIA RTX ray-query shadows active: %u triangles\n",
			rt.index_count / 3);
		rt.active_logged = qtrue;
	}
	return qtrue;
}
