#include "pt_world_mirror.h"

static struct {
    const msurface_t *surface;
    vec3_t normal, mins, maxs;
    float distance;
} pt_portals[PT_MAX_PORTALS];
static uint32_t pt_portal_count;

qboolean vk_pt_portal_shader(const shader_t *shader)
{
    for (uint32_t i = 0; i < pt_portal_count; ++i) {
        const shader_t *owner = pt_portals[i].surface->shader;
        if (owner == shader || owner->remappedShader == shader) return qtrue;
    }
    return qfalse;
}

static void portal_load_world(void)
{
    const bmodel_t *world = &tr.world->bmodels[0];
    pt_portal_count = 0;
    for (int i = 0; i < world->numSurfaces; ++i) {
        const msurface_t *surface = &world->firstSurface[i];
        if (world_surface_portal_kind(surface) != 2) continue;
        if (pt_portal_count == PT_MAX_PORTALS)
            ri.Error(ERR_DROP, "Path tracing: camera portal capacity exceeded");
        uint32_t id = pt_portal_count++;
        pt_portals[id].surface = surface;
        world_portal_geometry(surface, pt_portals[id].normal, &pt_portals[id].distance,
            pt_portals[id].mins, pt_portals[id].maxs);
    }
    ri.Printf(PRINT_ALL, "Path tracing: %u BSP camera portal surfaces loaded\n", pt_portal_count);
}

static uint32_t portal_world_flags(const msurface_t *surface)
{
    for (uint32_t i = 0; i < pt_portal_count; ++i)
        if (pt_portals[i].surface == surface) return (i+1u)<<16;
    return 0;
}

// Deformed BSP surfaces may share a tessellation batch. Associate each
// triangle by its shader and undeformed bounds, not by a shader-name guess.
static uint32_t portal_triangle_flags(const shader_t *shader, const float *a,
    const float *b, const float *c)
{
    uint32_t result = 0;
    float nearest = FLT_MAX;
    if (shader->sort != SS_PORTAL) return 0;
    for (uint32_t i = 0; i < pt_portal_count; ++i) {
        const shader_t *owner = pt_portals[i].surface->shader;
        if (owner != shader && owner->remappedShader != shader) continue;
        float squared = 0;
        for (int axis = 0; axis < 3; ++axis) {
            float center = (a[axis]+b[axis]+c[axis])/3;
            float d = fmaxf(pt_portals[i].mins[axis]-center, fmaxf(center-pt_portals[i].maxs[axis], 0));
            squared += d*d;
        }
        if (squared < nearest) { nearest = squared; result = (i+1u)<<16; }
    }
    return result;
}

static void portal_update(pt_lights_t *lights)
{
    memset(lights->portals, 0, sizeof(lights->portals));
    if (r_noportals->integer) return;
    for (uint32_t i = 0; i < pt_portal_count; ++i) {
        const refEntity_t *closest = NULL;
        float nearest = FLT_MAX;
        for (int j = 0; j < backEnd.refdef.num_entities; ++j) {
            const refEntity_t *entity = &backEnd.refdef.entities[j].e;
            if (entity->reType != RT_PORTALSURFACE ||
                fabsf(DotProduct(entity->origin, pt_portals[i].normal)-pt_portals[i].distance) > 64) continue;
            float squared = 0;
            for (int axis = 0; axis < 3; ++axis) {
                float d = fmaxf(pt_portals[i].mins[axis]-entity->origin[axis],
                    fmaxf(entity->origin[axis]-pt_portals[i].maxs[axis], 0));
                squared += d*d;
            }
            if (squared < nearest) { nearest = squared; closest = entity; }
        }
        if (closest && !VectorCompare(closest->origin, closest->oldorigin))
            pt_portal_transform(&lights->portals[i], pt_portals[i].normal,
                pt_portals[i].distance, closest, backEnd.refdef.rd.time);
    }
}
