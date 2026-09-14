/* Native post-effect package ABI. GPL-2.0-or-later. */
#ifndef VQ3_POSTFX_H
#define VQ3_POSTFX_H

#define PFX_MAX_EFFECTS 16
#define PFX_MAX_PARAMS 8
#define PFX_MAX_PASSES 4
#define PFX_ID 24
#define PFX_FILE 96
typedef struct {
    char id[PFX_ID], label[48];
    float initial, minimum, maximum, step;
} postfxParam_t;
typedef struct {
    int compute;
    char shader[PFX_FILE], vertex[PFX_FILE];
} postfxPass_t;
typedef struct {
    char id[PFX_ID], label[48], status[96];
    int numParams, numPasses, ready;
    postfxParam_t params[PFX_MAX_PARAMS];
    postfxPass_t passes[PFX_MAX_PASSES];
    char texture[PFX_FILE]; // Optional v2 package PNG at set 0, binding 2.
    int downsample; // v3: quarter-size float prepasses, then full-size composite.
    int depth; // v4: read-only scene depth + projection/jitter push constants.
    int motion; // v5: scene motion, validity/timing and camera reprojection rows.
} postfxEffect_t;

#endif
