#include <float.h>
/* Static BSP portal ownership. A shader's `portal` keyword alone cannot
 * distinguish a mirror from a remote camera, even when both share a shader.
 * Read a private entity cursor at world load; never consume the game cursor.
 * No per-frame work, material copies, or GPU storage is needed. */
static qboolean world_portal_geometry(const msurface_t *surface, vec3_t normal,
    float *plane_distance, vec3_t mins, vec3_t maxs)
{
    float distance;
    if (!surface || !surface->shader || surface->shader->sort != SS_PORTAL ||
        !surface->data || !tr.world || !tr.world->entityString) return qfalse;
    ClearBounds(mins, maxs);
    if (*surface->data == SF_FACE) {
        const srfSurfaceFace_t *face = (const srfSurfaceFace_t *)surface->data;
        if (face->numPoints < 3) return qfalse;
        VectorCopy(face->plane.normal, normal);
        distance = face->plane.dist;
        for (int i = 0; i < face->numPoints; ++i) AddPointToBounds(face->points[i], mins, maxs);
    } else if (*surface->data == SF_TRIANGLES) {
        const srfTriangles_t *mesh = (const srfTriangles_t *)surface->data;
        vec4_t plane;
        if (mesh->numIndexes < 3 || mesh->numVerts < 3) return qfalse;
        for (int i = 0; i < 3; ++i)
            if ((unsigned)mesh->indexes[i] >= (unsigned)mesh->numVerts) return qfalse;
        if (!PlaneFromPoints(plane, mesh->verts[mesh->indexes[0]].xyz,
            mesh->verts[mesh->indexes[1]].xyz, mesh->verts[mesh->indexes[2]].xyz)) return qfalse;
        VectorCopy(plane, normal); distance = plane[3];
        for (int i = 0; i < mesh->numVerts; ++i) {
            if (fabsf(DotProduct(mesh->verts[i].xyz, normal)-distance) > 0.1f) return qfalse;
            AddPointToBounds(mesh->verts[i].xyz, mins, maxs);
        }
    } else return qfalse; // Curved/deformed portals are not planar mirrors.

    *plane_distance = distance;
    return qtrue;
}

// 0: no owner, 1: mirror, 2: remote camera. Geometry is the undeformed BSP
// plane; a wave-deformed aperture must not wobble its camera orientation.
static int world_surface_portal_kind(const msurface_t *surface)
{
    vec3_t normal, mins, maxs;
    float distance, nearest = FLT_MAX;
    int kind = 0;
    if (!world_portal_geometry(surface, normal, &distance, mins, maxs)) return 0;
    char *cursor = tr.world->entityString;
    const char *token;
    while (*(token = R_ParseExt(&cursor, qtrue))) {
        char classname[MAX_QPATH] = "", target[MAX_QPATH] = "";
        vec3_t origin = {0, 0, 0};
        qboolean has_origin = qfalse;
        if (strcmp(token, "{")) return qfalse;
        while (*(token = R_ParseExt(&cursor, qtrue)) && strcmp(token, "}")) {
            char key[MAX_TOKEN_CHARS];
            Q_strncpyz(key, token, sizeof(key));
            token = R_ParseExt(&cursor, qtrue);
            if (!Q_stricmp(key, "classname")) Q_strncpyz(classname, token, sizeof(classname));
            else if (!Q_stricmp(key, "target")) Q_strncpyz(target, token, sizeof(target));
            else if (!Q_stricmp(key, "origin"))
                has_origin = sscanf(token, "%f %f %f", &origin[0], &origin[1], &origin[2]) == 3;
        }
        if (strcmp(token, "}")) return qfalse;
        if (Q_stricmp(classname, "misc_portal_surface") || !has_origin) continue;
        // Same 64-unit plane tolerance as the original portal renderer. When
        // several entities share a plane, prefer the nearest surface bounds
        // instead of accidentally borrowing a distant camera's identity.
        if (fabsf(DotProduct(origin, normal)-distance) > 64) continue;
        float squared = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float delta = fmaxf(mins[axis]-origin[axis], fmaxf(origin[axis]-maxs[axis], 0));
            squared += delta*delta;
        }
        if (squared < nearest) { nearest = squared; kind = target[0] ? 2 : 1; }
    }
    return kind;
}

static qboolean world_surface_is_mirror(const msurface_t *surface)
{
    return world_surface_portal_kind(surface) == 1;
}
