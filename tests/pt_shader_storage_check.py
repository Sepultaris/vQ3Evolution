"""Reject invocation-local copies of the large SSBO material/layer records."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("binary", type=Path)
parser.add_argument("--spirv-dis", default="spirv-dis")
args = parser.parse_args()
assembly = subprocess.run([args.spirv_dis, str(args.binary)], check=True,
                          capture_output=True, text=True).stdout
for name in ("MaterialRecord", "LayerRecord"):
    records = re.findall(r'OpName\s+(%\w+)\s+"'+name+r'"', assembly)
    assert records, f"Missing {name}: provide the non-stripped development SPIR-V"
    for record in records:
        assert not re.search(r"OpLoad\s+"+re.escape(record)+r"\s", assembly), f"Full {name} load"
        assert not re.search(r"OpTypePointer\s+Function\s+"+re.escape(record)+r"(?:\s|$)", assembly), \
            f"Invocation-local {name} copy"
print("PASS: material/layer records stay in GPU storage; no full-record loads or local copies")
