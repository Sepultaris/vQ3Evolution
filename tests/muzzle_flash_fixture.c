#include "../code/qcommon/q_shared.h"
#include "../code/renderercommon/tr_types.h"
#include "../code/renderercommon/muzzle_flash.h"
#include "../code/renderer_vulkan/pt_weapon_flags.h"
#include "../code/renderer_vulkan/pt_weapon_flags.h"
#include <assert.h>

int main(void) {
    floatint_t v;
    assert(R_MuzzleFlashScale(1)==1 && R_MuzzleFlashScale(0)==0);
    assert(R_MuzzleFlashScale(.25f)==.25f && R_MuzzleFlashScale(2)==2);
    assert(R_MuzzleFlashScale(-1)==0 && R_MuzzleFlashScale(99)==8);
    v.ui=0x7fc00000u; assert(R_MuzzleFlashScale(v.f)==1);
    v.ui=0x7f800000u; assert(R_MuzzleFlashScale(v.f)==8);
    v.ui=0xff800000u; assert(R_MuzzleFlashScale(v.f)==0);
    assert(pt_weapon_flags(0)==0 && pt_weapon_flags(RF_ROCKET)==0x02000000u);
    assert(pt_weapon_flags(RF_ROCKET_EXPLOSION)==0x01000000u);
    assert(pt_weapon_flags(RF_LIGHTNING_GUN)==0x00800000u);
    assert(pt_weapon_flags(RF_LIGHTNING_GUN|RF_MUZZLE_FLASH)==0x04800000u);
    assert((RF_ROCKET_EXPLOSION & (RF_ROCKET|RF_MUZZLE_FLASH|RF_LIGHTNING_GUN))==0);
    assert((RF_LIGHTNING_GUN & (RF_ROCKET|RF_MUZZLE_FLASH))==0);
    assert(pt_weapon_flags(0)==0 && pt_weapon_flags(RF_ROCKET)==0x02000000u);
    assert(pt_weapon_flags(RF_ROCKET_EXPLOSION)==0x01000000u);
    assert(pt_weapon_flags(RF_LIGHTNING_GUN)==0x00800000u);
    assert(pt_weapon_flags(RF_LIGHTNING_GUN|RF_MUZZLE_FLASH)==0x04800000u);
    assert((RF_ROCKET_EXPLOSION & (RF_ROCKET|RF_MUZZLE_FLASH|RF_LIGHTNING_GUN))==0);
    assert((RF_LIGHTNING_GUN & (RF_ROCKET|RF_MUZZLE_FLASH))==0);
    assert((RF_MUZZLE_FLASH & (RF_MINLIGHT|RF_THIRD_PERSON|RF_FIRST_PERSON|
        RF_DEPTHHACK|RF_NOSHADOW|RF_LIGHTING_ORIGIN|RF_SHADOW_PLANE|RF_WRAP_FRAMES))==0);
    assert((RF_ROCKET & (RF_MUZZLE_FLASH|RF_MINLIGHT|RF_THIRD_PERSON|RF_FIRST_PERSON|
        RF_DEPTHHACK|RF_CROSSHAIR|RF_NOSHADOW|RF_LIGHTING_ORIGIN|RF_SHADOW_PLANE|RF_WRAP_FRAMES))==0);
    puts("PASS: muzzle-flash scale defaults, independent dim/boost/off values, finite bounds and non-overlapping tag");
    return 0;
}
