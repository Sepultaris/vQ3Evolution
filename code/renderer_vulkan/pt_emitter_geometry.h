/* Packed copies of submitted world-space positions, never recomputed normals
 * or quantized geometry. The shader retains its original floating-point math. */
typedef struct { float a[4], b[4], c[4]; } pt_emitter_geometry_t;
static void cache_emitter_geometry(pt_emitter_geometry_t *geometry,
    const float *a, const float *b, const float *c, float power)
{
    memcpy(geometry->a, a, 3*sizeof(float)); geometry->a[3] = power;
    memcpy(geometry->b, b, 3*sizeof(float)); geometry->b[3] = 0;
    memcpy(geometry->c, c, 3*sizeof(float)); geometry->c[3] = 0;
}
