"""Locate stock BSP water/glass surfaces for repeatable in-game transport checks."""
import argparse
import struct
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("pak")
parser.add_argument("maps", nargs="*")
args = parser.parse_args()
with zipfile.ZipFile(args.pak) as pak:
    names = args.maps or ["q3dm1", "q3dm4", "q3dm7", "q3dm8", "q3dm11"]
    for name in names:
        data = pak.read(f"maps/{name}.bsp")
        def lump(index):
            start, length = struct.unpack_from("<2i", data, 8+index*8)
            return data[start:start+length]
        shaders = list(struct.iter_unpack("<64s2i", lump(1)))
        vertices = list(struct.iter_unpack("<10f4B", lump(10)))
        printed = set()
        for surface in struct.iter_unpack("<12i12f2i", lump(13)):
            shader_name, flags, contents = shaders[surface[0]]
            shader_name = shader_name.split(b"\0")[0].decode()
            if not (contents & 32 or "glass" in shader_name) or shader_name in printed:
                continue
            points = vertices[surface[3]:surface[3]+surface[4]]
            if not points:
                continue
            center = tuple(round(sum(p[i] for p in points)/len(points), 1) for i in range(3))
            print(name, shader_name, f"flags={flags:x} contents={contents:x}", center)
            printed.add(shader_name)
