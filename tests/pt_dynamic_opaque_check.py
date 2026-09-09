"""Compile the production dynamic partition; no game or GPU is used."""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "code/renderer_vulkan/vk_pathtrace.c"

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc")
    args = parser.parse_args()
    source = SOURCE.read_text()
    begin = source.index("static qboolean dynamic_opaque(")
    end = source.index("/* Native materials", begin)
    fixture = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef int qboolean;
typedef struct { float params[4], surface[4], emission[4]; } pt_material_t;
typedef struct { int integer; } cvar_t;
static cvar_t setting, *r_pathTracingDynamicOpaque=&setting;
static struct {
    uint32_t world_indices, *partition_indices, *partition_materials;
    struct { void *mapped; } triangle_materials, materials;
} pt;
'''
    fixture += source[begin:end]
    fixture += r'''
int main(void) {
    pt_material_t mat[7]={0};
    mat[1].params[0]=2; /* glow */
    mat[2].params[0]=4; /* glass */
    mat[3].surface[3]=1; /* cutout */
    mat[4].emission[2]=1; /* sky */
    mat[5].params[0]=3; /* multiplicative filter */
    mat[6].params[0]=1; /* blended coverage */
    for(uint32_t bits=0;bits<32;++bits) {
        uint32_t flags=((bits&1)?0x80000000u:0)|((bits&2)?0x40000000u:0)|
            ((bits&4)?0x20000000u:0)|((bits&8)?0x10000000u:0)|((bits&16)?0x08000000u:0);
        int expected=!(bits&16) && ((bits&4) || !(bits&3));
        assert(dynamic_opaque(flags,&mat[0])==expected);
        for(int m=1;m<7;++m) assert(!dynamic_opaque(flags|m,&mat[m]));
    }
    /* Mixed regular/weapon, flags, all-empty/all-opaque and disabled cases. */
    for(int enabled=0;enabled<2;++enabled) for(uint32_t count=0;count<=40;++count) {
        uint32_t indices[126], materials[42], tmp[126], tmpmat[42], original[42], starts[5];
        setting.integer=enabled;
        pt.world_indices=6; pt.partition_indices=tmp; pt.partition_materials=tmpmat;
        pt.triangle_materials.mapped=materials; pt.materials.mapped=mat;
        for(uint32_t i=0;i<count+2;++i) {
            indices[3*i]=3*i; indices[3*i+1]=3*i+1; indices[3*i+2]=3*i+2;
            materials[i]=(i%7)|((i%3==0)?0x20000000u:0)|((i%7==0)?0x08000000u:0);
            original[i]=materials[i];
        }
        vk_pt_partition_dynamic(indices,(count+2)*3,starts);
        assert(starts[0]==0 && starts[1]==6);
        assert(indices[0]==0 && indices[3]==3 && materials[0]==original[0]);
        uint32_t seen=0;
        for(int bucket=0;bucket<4;++bucket) {
            uint32_t first=starts[bucket+1]/3, last=bucket==3?count+2:starts[bucket+2]/3;
            assert(first<=last && last<=count+2);
            uint32_t previous=0;
            for(uint32_t i=first;i<last;++i) {
                uint32_t old=indices[3*i]/3, flags=materials[i];
                assert(old>=2 && old<count+2 && old>previous); previous=old;
                assert(indices[3*i+1]==old*3+1 && indices[3*i+2]==old*3+2);
                assert(flags==original[old]);
                int group=(flags&0x20000000u)?2:0;
                if(!enabled || !dynamic_opaque(flags,&mat[flags&0xffffu])) ++group;
                assert(group==bucket); ++seen;
                /* Instance custom index + local primitive keeps original material lookup. */
                assert(starts[bucket+1]/3+(i-first)==i);
            }
        }
        assert(seen==count);
        if(!enabled) assert(starts[1]==starts[2] && starts[3]==starts[4]);
    }
    puts("PASS: flags, materials, empty ranges, stable partition, primitive addressing, disabled path");
}
'''
    with tempfile.TemporaryDirectory(prefix="vq3-opaque-") as directory:
        path = Path(directory)
        (path / "fixture.c").write_text(fixture)
        for optimize in ("-O1", "-O3"):
            exe = path / "fixture.exe"
            subprocess.run([args.cc, optimize, str(path / "fixture.c"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
    rt = (ROOT / "code/renderer_vulkan/vk_raytracing.c").read_text()
    assert "5 * sizeof(VkAccelerationStructureInstanceKHR)" in rt
    assert "const uint32_t masks[5] = {7, 7, 23, 8, 8}" in rt
    assert "destroy_as(&rt.opaque_blas)" in rt and "destroy_as(&rt.opaque_weapon_blas)" in rt
    assert "instances[instance_count].instanceCustomIndex = starts[i]/3" in rt
    print("PASS: instance capacity, decal/weapon masks, cleanup contracts")

if __name__ == "__main__":
    main()
