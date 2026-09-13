// Core Vulkan storage-buffer traversal. No ray-query extension or device addresses.
layout(set = 0, binding = 0, std430) readonly buffer BVHNodes { vec4 nodes[]; };
layout(set = 0, binding = 1, std430) readonly buffer Vertices { float verts[]; };
layout(set = 0, binding = 2, std430) readonly buffer Indices { uint inds[]; };
layout(set = 0, binding = 3, std430) readonly buffer TriList { uint triList[]; };

const uint BVH_LEAF_FLAG = 0x80000000u;
// CPU construction stops at depth 40. One pending sibling per level fits here.
const int BVH_STACK = 48;

bool intersectAABB(uint node, vec3 origin, vec3 dir, float tMax, out float tNear)
{
	vec3 lo = nodes[2u * node].xyz;
	vec3 hi = nodes[2u * node + 1u].xyz;
	tNear = 0.0;
	float tFar = tMax;
	for (int axis = 0; axis < 3; ++axis) {
		// Parallel rays on a slab boundary must not produce 0 * infinity (NaN).
		if (dir[axis] == 0.0) {
			if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) return false;
		} else {
			float a = (lo[axis] - origin[axis]) / dir[axis];
			float b = (hi[axis] - origin[axis]) / dir[axis];
			tNear = max(tNear, min(a, b));
			tFar = min(tFar, max(a, b));
			if (tNear > tFar) return false;
		}
	}
	return tNear <= tFar;
}

bool intersectTriangleT(uint tri, vec3 origin, vec3 dir, float tMax, out float t, out vec3 normal)
{
	uint ia = inds[tri * 3u] * 3u;
	uint ib = inds[tri * 3u + 1u] * 3u;
	uint ic = inds[tri * 3u + 2u] * 3u;
	vec3 a = vec3(verts[ia], verts[ia + 1u], verts[ia + 2u]);
	vec3 e1 = vec3(verts[ib], verts[ib + 1u], verts[ib + 2u]) - a;
	vec3 e2 = vec3(verts[ic], verts[ic + 1u], verts[ic + 2u]) - a;
	vec3 p = cross(dir, e2);
	float det = dot(e1, p);
	if (abs(det) < 1e-9) return false;
	vec3 s = origin - a;
	float u = dot(s, p) / det;
	if (u < 0.0 || u > 1.0) return false;
	vec3 q = cross(s, e1);
	float v = dot(dir, q) / det;
	if (v < 0.0 || u + v > 1.0) return false;
	t = dot(e2, q) / det;
	normal = cross(e1, e2);
	return t > 1e-6 && t < tMax;
}

// Returns the closest hit, or any hit for occlusion queries; -1 means a miss.
float traceTree(uint root, uint listBase, vec3 origin, vec3 dir, float tMax, bool anyHit, out vec3 normal)
{
	normal = vec3(0.0);
	float nearRoot;
	if (!intersectAABB(root, origin, dir, tMax, nearRoot)) return -1.0;
	uint stack[BVH_STACK];
	uint sp = 0u;
	uint node = root;
	float best = -1.0;
	while (true) {
		vec4 lo = nodes[2u * node];
		vec4 hi = nodes[2u * node + 1u];
		uint left = floatBitsToUint(lo.w);
		uint right = floatBitsToUint(hi.w);
		if ((right & BVH_LEAF_FLAG) != 0u) {
			uint count = right & ~BVH_LEAF_FLAG;
			for (uint i = 0u; i < count; ++i) {
				float t;
				vec3 n;
				if (intersectTriangleT(triList[listBase + left + i], origin, dir, tMax, t, n)) {
					normal = n;
					if (anyHit) return t;
					best = t;
					tMax = t;
				}
			}
		} else {
			float tLeft, tRight;
			bool hitLeft = intersectAABB(left, origin, dir, tMax, tLeft);
			bool hitRight = intersectAABB(right, origin, dir, tMax, tRight);
			if (hitLeft && hitRight) {
				stack[sp++] = tLeft <= tRight ? right : left;
				node = tLeft <= tRight ? left : right;
				continue;
			}
			if (hitLeft || hitRight) {
				node = hitLeft ? left : right;
				continue;
			}
		}
		if (sp == 0u) return best;
		node = stack[--sp];
	}
}

float traceSceneNormal(vec4 trees, vec3 origin, vec3 dir, float tMax, bool anyHit, out vec3 normal)
{
	normal = vec3(0.0);
	float t = trees.w > 0.5 ? traceTree(0u, 0u, origin, dir, tMax, anyHit, normal) : -1.0;
	if (t >= 0.0) {
		if (anyHit) return t;
		tMax = t;
	}
	if (trees.y > 0.5) {
		vec3 n;
		float dynamicT = traceTree(uint(trees.x), uint(trees.z), origin, dir, tMax, anyHit, n);
		if (dynamicT >= 0.0) { t = dynamicT; normal = n; }
	}
	return t;
}

float traceScene(vec4 trees, vec3 origin, vec3 dir, float tMax, bool anyHit)
{
	vec3 unused;
	return traceSceneNormal(trees, origin, dir, tMax, anyHit, unused);
}
