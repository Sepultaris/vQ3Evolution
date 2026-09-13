/* Record changed push constants with a bounded FILE lifetime. No stream may
 * survive this function, including across maps and renderer restarts. */
static void pt_push_debug_record(const float *c, uint32_t mode, uint32_t samples,
    uint32_t triangle_count)
{
    static uint32_t last_mode=~0u, last_samples=~0u;
    static qboolean failed;
    FILE *file;
    int k;
    if (failed || (mode==last_mode && samples==last_samples)) return;
    last_mode=mode; last_samples=samples;
    file=fopen("pt_push_debug.log","ab");
    if (!file) { failed=qtrue; return; }
    fprintf(file, "PT c[0..29] sun=(%.4f,%.4f,%.4f) color=(%.3f,%.3f,%.3f) "
        "bias=%.3f tMax=%.0f near=%.2f far=%.0f proj0=%.3f proj5=%.3f "
        "proj8=%.3f proj9=%.3f exp=%.3f  tris=%u\n",
        c[16], c[17], c[18], c[24], c[25], c[26],
        c[20], c[21], c[3], c[7], c[11], c[15], c[22], c[23], c[19],
        triangle_count);
    for (k=0; k<30; ++k) fprintf(file,"%.8e ",c[k]);
    fprintf(file,"\n");
    if (fclose(file)!=0) failed=qtrue;
}
