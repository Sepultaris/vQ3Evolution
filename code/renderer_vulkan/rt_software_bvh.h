#ifndef RT_SOFTWARE_BVH_H
#define RT_SOFTWARE_BVH_H

/* CPU construction only; the GPU compute shader performs all ray traversal. */
#include <stdint.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

#define RT_BVH_LEAF_FLAG 0x80000000u
#define RT_BVH_BINS 8
#define RT_BVH_LEAF_TRIS 4
#define RT_BVH_MAX_DEPTH 40

/* Software BVH node: two tightly packed float4s. The compute kernel reads
 * nodes[2i] as AABB min.xyz + left child or first triangle-list entry, and
 * nodes[2i+1] as AABB max.xyz + right child or triangle count (the high bit
 * of right is set on leaves). */
typedef struct {
	float mn[3];
	uint32_t left;
	float mx[3];
	uint32_t right;
} rt_bvh_node_t;

/* ---------------------------------------------------------------------------
 * Software BVH construction. Binned-SAH top-down trees are built on the CPU
 * over the exact triangle ranges that rt_shadows_cpu.comp traverses. The
 * builder is shared by the static world tree (built once per map load) and
 * the dynamic opaque tree (rebuilt each frame from the host mirrors).
 * ------------------------------------------------------------------------- */

typedef struct {
	const float *vertices;   /* float3 positions, world or full mirror */
	const uint32_t *indices; /* triangle index array, world or full mirror */
	uint32_t global_first;   /* first GLOBAL triangle index of this slice */
} rt_bvh_scene_t;

typedef struct {
	rt_bvh_node_t *nodes;
	uint32_t cursor;
	uint32_t node_base; /* absolute node index of this slice within the buffer */
	uint32_t *tli; /* triangle -> global index indirection, filled per leaf */
	uint32_t tli_cursor;
} rt_bvh_output_t;

static void rt_bvh_triangle_bounds(const rt_bvh_scene_t *scene, uint32_t local_tri,
	float (*out)[6])
{
	uint32_t base = (scene->global_first + local_tri) * 3;
	const uint32_t a = scene->indices[base + 0];
	const uint32_t b = scene->indices[base + 1];
	const uint32_t c = scene->indices[base + 2];
	const float *va = &scene->vertices[a * 3];
	const float *vb = &scene->vertices[b * 3];
	const float *vc = &scene->vertices[c * 3];
	(*out)[0] = fminf(fminf(va[0], vb[0]), vc[0]);
	(*out)[1] = fminf(fminf(va[1], vb[1]), vc[1]);
	(*out)[2] = fminf(fminf(va[2], vb[2]), vc[2]);
	(*out)[3] = fmaxf(fmaxf(va[0], vb[0]), vc[0]);
	(*out)[4] = fmaxf(fmaxf(va[1], vb[1]), vc[1]);
	(*out)[5] = fmaxf(fmaxf(va[2], vb[2]), vc[2]);
}

static float rt_bvh_surface_area(const float mn[3], const float mx[3])
{
	const float dx = mx[0] - mn[0];
	const float dy = mx[1] - mn[1];
	const float dz = mx[2] - mn[2];
	return 2.0f * (dx * dy + dx * dz + dy * dz);
}

static void rt_bvh_merge(float *mn, float *mx, const float other[6])
{
	mn[0] = fminf(mn[0], other[0]);
	mn[1] = fminf(mn[1], other[1]);
	mn[2] = fminf(mn[2], other[2]);
	mx[0] = fmaxf(mx[0], other[3]);
	mx[1] = fmaxf(mx[1], other[4]);
	mx[2] = fmaxf(mx[2], other[5]);
}

static void rt_bvh_merge3(float *mn, float *mx, const float p[3])
{
	mn[0] = fminf(mn[0], p[0]);
	mn[1] = fminf(mn[1], p[1]);
	mn[2] = fminf(mn[2], p[2]);
	mx[0] = fmaxf(mx[0], p[0]);
	mx[1] = fmaxf(mx[1], p[1]);
	mx[2] = fmaxf(mx[2], p[2]);
}

static void rt_bvh_range_bounds(const float (*bounds)[6], const uint32_t *order,
	uint32_t start, uint32_t count, float mn[3], float mx[3])
{
	uint32_t i;
	mn[0] = mn[1] = mn[2] = FLT_MAX;
	mx[0] = mx[1] = mx[2] = -FLT_MAX;
	for (i = 0; i < count; ++i)
		rt_bvh_merge(mn, mx, bounds[order[start + i]]);
}

static uint32_t rt_bvh_build_node(rt_bvh_output_t *out,
	const rt_bvh_scene_t *scene, const float (*bounds)[6],
	uint32_t *order, uint32_t start, uint32_t count, uint32_t depth)
{
	float mn[3], mx[3];
	float cmn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
	float cmx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
	uint32_t i;

	rt_bvh_range_bounds(bounds, order, start, count, mn, mx);
	for (i = 0; i < count; ++i) {
		const float *b = bounds[order[start + i]];
		for (uint32_t a = 0; a < 3; ++a) {
			cmn[a] = fminf(cmn[a], 0.5f * (b[a] + b[a + 3]));
			cmx[a] = fmaxf(cmx[a], 0.5f * (b[a] + b[a + 3]));
		}
	}

	float leaf_cost = (float)count;
	int best_axis = -1;
	int best_bin = 0;
	float best_cost = FLT_MAX;
	float parent_area = rt_bvh_surface_area(mn, mx);
	if (count > RT_BVH_LEAF_TRIS && depth < RT_BVH_MAX_DEPTH &&
		parent_area > 0.0f) {
		for (int axis = 0; axis < 3; ++axis) {
			float bin_mn[RT_BVH_BINS][3];
			float bin_mx[RT_BVH_BINS][3];
			uint32_t bin_count[RT_BVH_BINS];
			float span = cmx[axis] - cmn[axis];
			int b;
			if (span <= 0.0f)
				continue;
			for (b = 0; b < RT_BVH_BINS; ++b) {
				bin_mn[b][0] = bin_mn[b][1] = bin_mn[b][2] = FLT_MAX;
				bin_mx[b][0] = bin_mx[b][1] = bin_mx[b][2] = -FLT_MAX;
				bin_count[b] = 0;
			}
			for (i = 0; i < count; ++i) {
				const uint32_t tri = order[start + i];
				const float *b = bounds[tri];
				const int bin = (int)(((0.5f * (b[axis] + b[axis + 3]) - cmn[axis]) *
					(float)RT_BVH_BINS) / span);
				const int clamped = bin < 0 ? 0 : (bin >= RT_BVH_BINS ? RT_BVH_BINS - 1 : bin);
				rt_bvh_merge3(bin_mn[clamped], bin_mx[clamped], &b[0]);
				rt_bvh_merge3(bin_mn[clamped], bin_mx[clamped], &b[3]);
				++bin_count[clamped];
			}
			float lmn[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
			float lmx[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			uint32_t left_count = 0;
			for (b = 0; b < RT_BVH_BINS - 1; ++b) {
				float rmn[3], rmx[3];
				uint32_t right_count;
				float cost;
				if (bin_count[b]) {
					rt_bvh_merge3(lmn, lmx, bin_mn[b]);
					rt_bvh_merge3(lmn, lmx, bin_mx[b]);
				}
				left_count += bin_count[b];
				right_count = count - left_count;
				if (!left_count || !right_count)
					continue;
				rmn[0] = rmn[1] = rmn[2] = FLT_MAX;
				rmx[0] = rmx[1] = rmx[2] = -FLT_MAX;
				for (int r = b + 1; r < RT_BVH_BINS; ++r) {
					if (!bin_count[r]) continue;
					rt_bvh_merge3(rmn, rmx, bin_mn[r]);
					rt_bvh_merge3(rmn, rmx, bin_mx[r]);
				}
				cost = 1.0f + ((float)left_count * rt_bvh_surface_area(lmn, lmx) +
					(float)right_count * rt_bvh_surface_area(rmn, rmx)) /
					parent_area;
				if (cost < best_cost) {
					best_cost = cost;
					best_axis = axis;
					best_bin = b;
				}
			}
		}
	}

	if (best_axis < 0 || best_cost >= leaf_cost) {
		rt_bvh_node_t *node = &out->nodes[out->cursor];
		uint32_t index = out->cursor++;
		uint32_t base = out->tli_cursor;
		for (i = 0; i < count; ++i)
			out->tli[base + i] = scene->global_first + order[start + i];
		out->tli_cursor += count;
		node->mn[0] = mn[0]; node->mn[1] = mn[1]; node->mn[2] = mn[2];
		node->mx[0] = mx[0]; node->mx[1] = mx[1]; node->mx[2] = mx[2];
		node->left = base;
		node->right = count | RT_BVH_LEAF_FLAG;
		return index;
	}

	{
		uint32_t lo = start;
		uint32_t hi = start + count;
		while (lo < hi) {
			const uint32_t tri = order[lo];
			const float *b = bounds[tri];
			const int bin = (int)(((0.5f * (b[best_axis] + b[best_axis + 3]) - cmn[best_axis]) *
				(float)RT_BVH_BINS) / (cmx[best_axis] - cmn[best_axis]));
			if ((bin < 0 ? 0 : (bin >= RT_BVH_BINS ? RT_BVH_BINS - 1 : bin)) <= best_bin) {
				++lo;
				continue;
			}
			{
				--hi;
				uint32_t t = order[lo];
				order[lo] = order[hi];
				order[hi] = t;
			}
		}
		{
			uint32_t mid = lo;
			/* Roundoff in binning must not create an empty child. */
			if (mid == start || mid == start + count) mid = start + count / 2;
			/* Pre-order emission: the parent occupies the lowest index of this
			 * subtree so the root is node 0 in every slice, matching the
			 * traversal root the compute kernel starts from. */
			rt_bvh_node_t *node = &out->nodes[out->cursor];
			uint32_t index = out->cursor++;
			node->mn[0] = mn[0]; node->mn[1] = mn[1]; node->mn[2] = mn[2];
			node->mx[0] = mx[0]; node->mx[1] = mx[1]; node->mx[2] = mx[2];
			node->left = out->node_base +
				rt_bvh_build_node(out, scene, bounds, order,
					start, mid - start, depth + 1);
			node->right = out->node_base +
				rt_bvh_build_node(out, scene, bounds, order,
					mid, start + count - mid, depth + 1);
			return index;
		}
	}
}

static int rt_bvh_build(rt_bvh_output_t *out, const rt_bvh_scene_t *scene,
	uint32_t triangle_count)
{
	float (*bounds)[6];
	uint32_t *order;
	uint32_t i;

	if (!triangle_count || !out->tli ||
		out->node_base + out->cursor + 2 * triangle_count > RT_MAX_TRIANGLES * 2 ||
		scene->global_first + out->tli_cursor + triangle_count > RT_MAX_TRIANGLES)
		return 0;
	bounds = (float (*)[6])malloc((size_t)triangle_count * 6 * sizeof(float));
	order = (uint32_t *)malloc((size_t)triangle_count * sizeof(uint32_t));
	if (!bounds || !order) {
		free(bounds);
		free(order);
		return 0;
	}
	for (i = 0; i < triangle_count; ++i) {
		rt_bvh_triangle_bounds(scene, i, &bounds[i]);
		order[i] = i;
	}
	rt_bvh_build_node(out, scene, bounds, order, 0, triangle_count, 0);
	free(bounds);
	free(order);
	return 1;
}

#endif
