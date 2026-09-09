/* Homogeneous convex BSP fog. Loaded once; uploaded once, never per frame. */
static qboolean fog_bound_plane(const fog_t *fog, const dplane_t *plane)
{
    // Only discard an exact duplicate of a bound we already clip on the GPU.
    // Non-axial planes and tighter axial cuts retain their authored halfspace.
    for (int axis = 0; axis < 3; ++axis) {
        float normal = LittleFloat(plane->normal[axis]);
        if ((normal == 1 || normal == -1) &&
            LittleFloat(plane->normal[(axis+1)%3]) == 0 &&
            LittleFloat(plane->normal[(axis+2)%3]) == 0 &&
            LittleFloat(plane->dist) == normal*fog->bounds[normal > 0][axis])
            return qtrue;
    }
    return qfalse;
}

static void fog_load(const dbrush_t *brushes, uint32_t brush_count,
    const dbrushside_t *sides, uint32_t side_count, const dplane_t *planes, uint32_t plane_count)
{
    uint32_t count = tr.world->numfogs > 0 ? tr.world->numfogs - 1 : 0;
    uint32_t total = 0, cursor = 0;
    if (count > MAX_MAP_FOGS) ri.Error(ERR_DROP, "Path tracing: too many fog volumes");
    for (uint32_t i = 0; i < count; ++i) {
        const fog_t *fog = tr.world->fogs + i + 1;
        uint32_t brush = fog->originalBrushNumber;
        if (brush >= brush_count) ri.Error(ERR_DROP, "Path tracing: invalid fog brush");
        uint32_t first = LittleLong(brushes[brush].firstSide), n = LittleLong(brushes[brush].numSides);
        if (n < 4 || n > MAX_MAP_BRUSHSIDES || first > side_count || n > side_count-first || total > MAX_MAP_BRUSHSIDES-n)
            ri.Error(ERR_DROP, "Path tracing: invalid fog sides");
        for (uint32_t j = 0; j < n; ++j) {
            uint32_t index = LittleLong(sides[first+j].planeNum);
            if (index >= plane_count)
                ri.Error(ERR_DROP, "Path tracing: invalid fog plane");
            if (!fog_bound_plane(fog, planes+index)) ++total;
        }
    }
    VkDeviceSize bytes = sizeof(pt_fog_header_t) + total*sizeof(vec4_t);
    if (pt.fog.size != bytes) {
        pt_buffer_t replacement = {0};
        if (!buffer_create(&replacement, bytes, qtrue)) {
            buffer_destroy(&replacement);
            ri.Error(ERR_DROP, "Path tracing: cannot allocate fog volumes");
        }
        // Map-load boundary only. Never retire a buffer still used by a frame.
        VK_CHECK(qvkDeviceWaitIdle(vk.device));
        buffer_destroy(&pt.fog);
        pt.fog = replacement;
        bind_buffer(48, pt.fog.buffer, pt.fog.size);
    }
    memset(pt.fog.mapped, 0, (size_t)bytes);
    pt_fog_header_t *data = pt.fog.mapped;
    vec4_t *out_planes = (vec4_t *)(data+1);
    data->control[0] = count; data->control[1] = total;
    ClearBounds(data->mins, data->maxs);
    for (uint32_t i = 0; i < count; ++i) {
        const fog_t *fog = tr.world->fogs+i+1;
        const dbrush_t *brush = brushes+fog->originalBrushNumber;
        uint32_t first = LittleLong(brush->firstSide), n = LittleLong(brush->numSides);
        uint32_t shader_index = LittleLong(brush->shaderNum);
        if (shader_index >= tr.world->numShaders) ri.Error(ERR_DROP, "Path tracing: invalid fog shader");
        shader_t *shader = R_FindShader(tr.world->shaders[shader_index].shader, LIGHTMAP_NONE, qtrue);
        pt_fog_t *out = data->volumes+i;
        VectorCopy(fog->bounds[0], out->mins); VectorCopy(fog->bounds[1], out->maxs);
        AddPointToBounds(fog->bounds[0], data->mins, data->maxs);
        AddPointToBounds(fog->bounds[1], data->mins, data->maxs);
        // Physical interpretation: 99% extinction at the authored opaque depth.
        // This deliberately is not Quake's screen-space fog lookup curve.
        out->mins[3] = fog->parms.depthForOpaque > 0 ? 4.605170186f/MAX(fog->parms.depthForOpaque,1) : 0;
        for (int c = 0; c < 3; ++c) {
            out->albedo[c] = powf(Com_Clamp(0, 1, fog->parms.color[c]), 2.2f);
            // Collision estimator divides the emission coefficient by sigma_t.
            out->emission[c] = out->albedo[c]*MAX(shader->rtSurfaceLight,0)/1000.0f;
        }
        out->planes[0] = cursor;
        for (uint32_t j = 0; j < n; ++j) {
            uint32_t index = LittleLong(sides[first+j].planeNum);
            if (fog_bound_plane(fog, planes+index)) continue;
            for (int c = 0; c < 3; ++c) out_planes[cursor][c] = LittleFloat(planes[index].normal[c]);
            out_planes[cursor++][3] = LittleFloat(planes[index].dist);
        }
        out->planes[1] = cursor-out->planes[0];
    }
    pt.fog_dirty = qtrue;
    pt.temporal_valid = qfalse;
    ri.Printf(PRINT_ALL, "Path tracing fog: %u convex volumes, %u planes, %llu bytes; scattering/extinction/emission enabled\n",
        count, total, (unsigned long long)bytes);
}
