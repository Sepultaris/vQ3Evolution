/* Complete each alias entry only after all probabilities/aliases are final.
 * Copy the stored float verbatim; never recompute or renormalize the PDF. */
static void cache_alias_probabilities(float (*entries)[4], uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
        entries[i][3] = entries[(uint32_t)entries[i][1]][2];
}
