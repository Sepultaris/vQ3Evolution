"""Check changed-light reaction is local, visible, and not a global reset."""
import argparse
import re
from pathlib import Path
import numpy as np
from pt_temporal_check import read_image


def check(directory, log):
    text=log.read_text(errors="replace")
    causes=re.findall(r"Temporal reset causes: renderer (\d+), invalid (\d+), lights (\d+), time (\d+), camera/settings (\d+)",text)
    updates=list(map(int,re.findall(r"reconstruction: (\d+) local light updates",text)))
    if len(causes)!=2 or causes[0]!=causes[1] or len(updates)!=2 or updates[1]<=updates[0]:
        raise SystemExit("FAIL: flash did not update locally without a global history reset")
    reactions=[]
    for i in range(12):
        r,g,b=np.moveaxis(read_image(directory,f"pt_local_flash_{i}"),-1,0)
        reactive=(r>150)&(g<100)
        stable=(g>180)&(r<60)&(b<60)
        reactions.append(reactive.mean())
        if stable.mean()<.1:
            raise SystemExit("FAIL: no unaffected surfaces retained during firing")
    print(f"Local reactive coverage across flashes: {min(reactions):.1%}–{max(reactions):.1%}; {updates[1]-updates[0]} light updates")
    if max(reactions)<.005 or max(reactions)>.85:
        raise SystemExit("FAIL: missing or screen-wide changed-light response")
    print("PASS: local changed-light response with unaffected history preserved")


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots",type=Path)
    parser.add_argument("log",type=Path)
    args=parser.parse_args()
    check(args.screenshots,args.log)
