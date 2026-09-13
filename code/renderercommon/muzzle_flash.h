#ifndef VQ3E_MUZZLE_FLASH_H
#define VQ3E_MUZZLE_FLASH_H

/* Shared by native/QVM cgame and the renderer. Integer NaN classification
 * remains valid in release builds compiled with -ffast-math. */
static float R_MuzzleFlashScale(float value)
{
    floatint_t bits;
    bits.f = value;
    if ((bits.ui & 0x7fffffffu) > 0x7f800000u) return 1.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 8.0f) return 8.0f;
    return value;
}
#endif
