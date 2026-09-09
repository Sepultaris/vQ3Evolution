/* Conservative acceleration of the EXISTING emitter CDF, not a replacement
 * distribution. Keep the same random draw, comparison and selected primitive.
 * Included after pt_emitter_t; also executed by the CPU regression fixture. */
#define PT_EMITTER_SEARCH_BUCKETS 64u
static void build_emitter_search(const pt_emitter_t *emitters, uint32_t count,
    float total, uint32_t ranges[PT_EMITTER_SEARCH_BUCKETS][2])
{
    uint32_t low = 0, high = 0;
    if (!count) {
        memset(ranges, 0, PT_EMITTER_SEARCH_BUCKETS * 2 * sizeof(uint32_t));
        return;
    }
    for (uint32_t bucket = 0; bucket < PT_EMITTER_SEARCH_BUCKETS; ++bucket) {
        /* Form boundaries in double precision, then expand by one float ULP.
         * u*total can round onto the next bucket's boundary. Inclusive upper
         * bounds and outward rounding preserve the old <= tie behavior. */
        float lower = nextafterf((float)((double)total * bucket / PT_EMITTER_SEARCH_BUCKETS), -INFINITY);
        float upper = nextafterf((float)((double)total * (bucket+1u) / PT_EMITTER_SEARCH_BUCKETS), INFINITY);
        while (low+1u < count && emitters[low].cumulative_power <= lower) ++low;
        if (high < low) high = low;
        while (high+1u < count && emitters[high].cumulative_power <= upper) ++high;
        ranges[bucket][0] = low;
        ranges[bucket][1] = high;
    }
}
