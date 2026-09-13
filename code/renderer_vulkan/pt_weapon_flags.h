#ifndef VQ3E_PT_WEAPON_FLAGS_H
#define VQ3E_PT_WEAPON_FLAGS_H
// Low 16 bits remain the material ID. No extra GPU vertex or per-ray storage.
static unsigned pt_weapon_flags(int renderfx) {
    unsigned flags = 0;
    if (renderfx & RF_MUZZLE_FLASH) flags |= 0x04000000u;
    if (renderfx & RF_ROCKET) flags |= 0x02000000u;
    if (renderfx & RF_ROCKET_EXPLOSION) flags |= 0x01000000u;
    if (renderfx & RF_LIGHTNING_GUN) flags |= 0x00800000u;
    return flags;
}
#endif
