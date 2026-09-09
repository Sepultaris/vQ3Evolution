/* Scheduling only: shader arithmetic, per-pixel seeds and budgets are unchanged. */
static uint32_t rr_workgroup_rows(uint32_t vendor, uint32_t max_x,
    uint32_t max_y, uint32_t max_threads, int requested)
{
    if (max_x < 8 || max_y < 8 || max_threads < 64) return 0;
    if ((requested == 8 || requested == 32 || requested == 64 || requested == 128) &&
        (uint32_t)requested <= max_y && (uint32_t)requested * 8 <= max_threads)
        return (uint32_t)requested;
    if (vendor == 0x10de) {
        if (max_y >= 128 && max_threads >= 1024) return 128;
        if (max_y >= 64 && max_threads >= 512) return 64;
    }
    return 8;
}
