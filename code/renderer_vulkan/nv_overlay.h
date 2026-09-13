// Exact legacy screen-effect names. Item icons/models must not activate NV.
static qboolean R_IsNvOverlayName(const char *name) {
    static const char *const names[] = {
        "nvgScope2", "nvgBrightA", "nvgBrightB", "nvgStatic",
        "nvgScope", "nvgBright", "gfx/items/nvgStatic",
        "gfx/items/nvgScope", "gfx/items/nvgBright"
    };
    unsigned i;
    for (i = 0; i < ARRAY_LEN(names); ++i)
        if (!Q_stricmp(name, names[i])) return qtrue;
    return qfalse;
}
