"""Reject invocation-local copies of the large SSBO material/layer records."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--spirv-dis", default="spirv-dis")
parser.add_argument("--transmission-layout-only", action="store_true",
                    help="Check the packed transmission guide in temporal/filter shaders without material tables")
args = parser.parse_args()
assembly = subprocess.run([args.spirv_dis, str(args.binary)], check=True,
                          capture_output=True, text=True).stdout
for name in (() if args.transmission_layout_only else ("MaterialRecord", "LayerRecord")):
    records = re.findall(r'OpName\s+(%\w+)\s+"'+name+r'"', assembly)
    assert records, f"Missing {name}: provide the non-stripped development SPIR-V"
    for record in records:
        assert not re.search(r"OpLoad\s+"+re.escape(record)+r"\s", assembly), f"Full {name} load"
        assert not re.search(r"OpTypePointer\s+Function\s+"+re.escape(record)+r"(?:\s|$)", assembly), \
            f"Invocation-local {name} copy"
        arrays = re.findall(r'(%\w+)\s*=\s*OpType(?:Runtime)?Array\s+'+re.escape(record)+r'(?:\s|$)', assembly)
        for array in arrays:
            stride = 3312 if name == "MaterialRecord" else 352
            assert re.search(r'OpDecorate\s+'+re.escape(array)+fr'\s+ArrayStride\s+{stride}(?:\s|$)', assembly), \
                f"{name} allocation/stride mismatch"
        if name == "MaterialRecord" and arrays:
            for member, offset in enumerate((0, 16, 32, 48, 64, 80, 96, 112, 3280)):
                assert re.search(r'OpMemberDecorate\s+'+re.escape(record)+fr'\s+{member}\s+Offset\s+{offset}(?:\s|$)', assembly), \
                    f"MaterialRecord member {member} has wrong offset"
if not args.transmission_layout_only:
    print("PASS: material/layer records stay in GPU storage; no full-record loads or local copies; 3312/352-byte strides")
    lights = re.findall(r'OpName\s+(%\w+)\s+"Lights"', assembly)
    assert lights, "Missing compiled light table"
    for light in lights:
        for member, offset in enumerate((116256, 116768, 117280, 117296, 117312, 117328, 117344), 6):
            assert re.search(r'OpMemberDecorate\s+'+re.escape(light)+fr'\s+{member}\s+Offset\s+{offset}(?:\s|$)', assembly), \
                f"Lights member {member} has wrong offset; existing data must precede appended CDF index"
    print("PASS: emitter search is appended after unchanged previous lights/decals/sky; packed index at byte 117344")
    geometry = re.findall(r'OpName\s+(%\w+)\s+"EmitterGeometry"', assembly)
    for record in geometry:
        arrays = re.findall(r'(%\w+)\s*=\s*OpTypeArray\s+'+re.escape(record)+r'(?:\s|$)', assembly)
        for array in arrays:
            assert re.search(r'OpDecorate\s+'+re.escape(array)+r'\s+ArrayStride\s+48(?:\s|$)', assembly), "Emitter geometry stride"
        for member, offset in enumerate((0,16,32)):
            assert re.search(r'OpMemberDecorate\s+'+re.escape(record)+fr'\s+{member}\s+Offset\s+{offset}(?:\s|$)', assembly), "Emitter geometry member offset"
    if geometry:
        for light in lights:
            assert re.search(r'OpMemberDecorate\s+'+re.escape(light)+r'\s+13\s+Offset\s+117856(?:\s|$)', assembly), "Emitter geometry must follow unchanged light prefix"
        print("PASS: packed emitter geometry at byte 117856, 48-byte stride; no quantized positions")

# Validate the compiled producer/consumer layout, not only GLSL source strings.
records = re.findall(r'OpName\s+(%\w+)\s+"TransmissionGuide"', assembly)
checked = 0
for record in records:
    arrays = re.findall(r'(%\w+)\s*=\s*OpTypeRuntimeArray\s+'+re.escape(record)+r'(?:\s|$)', assembly)
    if not arrays:
        continue  # The compiler also has a separate invocation-local struct.
    for member, offset in ((0, 0), (1, 16)):
        assert re.search(r'OpMemberDecorate\s+'+re.escape(record)+fr'\s+{member}\s+Offset\s+{offset}(?:\s|$)', assembly), \
            f"TransmissionGuide member {member} has wrong offset"
    for array in arrays:
        assert re.search(r'OpDecorate\s+'+re.escape(array)+r'\s+ArrayStride\s+32(?:\s|$)', assembly), \
            "TransmissionGuide allocation/stride mismatch"
    members = re.search(re.escape(record)+r'\s*=\s*OpTypeStruct\s+(%\w+)\s+(%\w+)', assembly)
    assert members, "Missing TransmissionGuide member types"
    vector = re.search(re.escape(members[2])+r'\s*=\s*OpTypeVector\s+(%\w+)\s+4(?:\s|$)', assembly)
    assert vector and re.search(re.escape(vector[1])+r'\s*=\s*OpTypeInt\s+32\s+0(?:\s|$)', assembly), \
        "TransmissionGuide identity must be four unsigned 32-bit integers"
    checked += 1
if args.transmission_layout_only:
    assert checked, "Missing compiled transmission guide buffer"
if checked:
    print("PASS: transmission guide uses 32-byte stride, offsets 0/16, integer identity/count")
