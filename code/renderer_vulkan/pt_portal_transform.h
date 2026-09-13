/* Affine equivalent of R_GetPortalOrientations + R_MirrorPoint/Vector.
 * Four std430 vec4s; destination plane normal == 0 marks an inactive camera. */
#define PT_MAX_PORTALS 63
#define PT_PORTAL_MASK 0x003f0000u
typedef struct { float rows[3][4], clip[4]; } pt_portal_t;

static void pt_portal_transform(pt_portal_t *out, const vec3_t normal,
    float distance, const refEntity_t *entity, int time)
{
    vec3_t source[3], camera[3], pivot, rotated;
    float angle = 0;
    VectorCopy(normal, source[0]);
    PerpendicularVector(source[1], source[0]);
    CrossProduct(source[0], source[1], source[2]);
    VectorMA(entity->origin, distance-DotProduct(entity->origin, normal), normal, pivot);
    VectorScale(entity->axis[0], -1, camera[0]);
    VectorScale(entity->axis[1], -1, camera[1]);
    VectorCopy(entity->axis[2], camera[2]);
    if (entity->oldframe)
        angle = entity->frame ? time*0.001f*entity->frame : entity->skinNum+sinf(time*0.003f)*4;
    else angle = entity->skinNum;
    if (angle != 0) {
        VectorCopy(camera[1], rotated);
        RotatePointAroundVector(camera[1], camera[0], rotated, angle);
        CrossProduct(camera[0], camera[1], camera[2]);
    }
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col)
            out->rows[row][col] = camera[0][row]*source[0][col] +
                camera[1][row]*source[1][col] + camera[2][row]*source[2][col];
        out->rows[row][3] = entity->oldorigin[row]-DotProduct(out->rows[row], pivot);
        out->clip[row] = -camera[0][row];
    }
    out->clip[3] = DotProduct(out->clip, entity->oldorigin);
}
