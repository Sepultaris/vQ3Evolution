"""Report known NVIDIA-internal diagnostics; fail on any other Vulkan issue.

This does not silence the validation layer or certify a validation-clean run.
Only use fresh logs from a successfully completed isolated test.
"""
import argparse
import re
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    args = parser.parse_args()
    log = args.log.read_text(errors="replace")
    blocks = re.split(r"(?=Validation (?:Error|Warning):)", log)
    known = unknown = 0
    for block in blocks:
        if not block.startswith(("Validation Error:", "Validation Warning:")):
            continue
        header = block.splitlines()[0]
        pacer = "[nv.sl.dlss_g.cmdCtx.pacer.command-buffer]" in block
        layout = ("VUID-vkCmdDraw-None-09600" in header and pacer
                  and "[nv.sl.dlss_g.tex2d.fake-swapchain-buffer]" in block)
        semaphore = ("VUID-vkQueueSubmit-pSignalSemaphores-00067" in header
                     and "[nv.sl.dlss_g.cmdCtx.pacer.present-semaphore]" in block)
        fg_command = "[nv.sl.dlss_g.cmdCtx.dlssg.command-buffer]" in block
        fg_clear = ("SYNC-HAZARD-WRITE-AFTER-WRITE" in header and fg_command
                    and "[nv.ngx.dlssg.resource]" in block)
        fg_copy = ("SYNC-HAZARD-READ-AFTER-WRITE" in header and fg_command
                   and "[nv.sl.dlss_g.tex2d.fake-swapchain-buffer]" in block)
        if layout or semaphore or fg_clear or fg_copy:
            known += 1
            print("UNRESOLVED NVIDIA-INTERNAL:", header)
        else:
            unknown += 1
            print("UNEXPECTED:", block)
    print(f"{known} known unresolved diagnostics; {unknown} unexpected diagnostics")
    if unknown:
        raise SystemExit(1)
    if known:
        print("Regression gate passed, but this run is NOT validation-clean.")
    else:
        print("No validation diagnostics in this log; verify validation was enabled.")


if __name__ == "__main__":
    main()
