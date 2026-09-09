"""Report known NVIDIA-internal diagnostics; fail on any other Vulkan issue.

This does not silence the validation layer or certify a validation-clean run.
Only use fresh logs from a successfully completed isolated test.
"""
import argparse
import re
from pathlib import Path


def diagnostic_blocks(log, console=False):
    if not console:
        return [block for block in re.split(r"(?=Validation (?:Error|Warning):)", log)
                if block.startswith(("Validation Error:", "Validation Warning:"))]
    # The layer's file is reopened on every Vulkan instance. Streamline's
    # callback also sends validation to qconsole, which retains ALL restarts.
    blocks = []
    lines = log.splitlines()
    for index, line in enumerate(lines):
        if "[debugUtilsMessengerCallback]" not in line:
            continue
        message = line.split("[debugUtilsMessengerCallback]", 1)[1].strip()
        if re.fullmatch(r"\(Warning - This VUID has now been reported \d+ times, which is the "
                        r"duplicate_message_limit value, this will be the last time reporting it\)\.", message):
            continue  # Only the layer's duplicate notice; its original is retained.
        block = [message]
        for following in lines[index+1:]:
            if not following.strip() or "Streamline:" in following:
                break
            block.append(following)
        blocks.append("\n".join(block))
    return blocks


def known_nvidia_diagnostic(block):
    pacer = "[nv.sl.dlss_g.cmdCtx.pacer.command-buffer]" in block
    layout = ("VUID-vkCmdDraw-None-09600" in block and pacer
              and "[nv.sl.dlss_g.tex2d.fake-swapchain-buffer]" in block)
    semaphore = ("VUID-vkQueueSubmit-pSignalSemaphores-00067" in block
                 and "[nv.sl.dlss_g.cmdCtx.pacer.present-semaphore]" in block)
    fg_command = "[nv.sl.dlss_g.cmdCtx.dlssg.command-buffer]" in block
    fg_clear = ("SYNC-HAZARD-WRITE-AFTER-WRITE" in block and fg_command
                and "[nv.ngx.dlssg.resource]" in block)
    fg_copy = ("SYNC-HAZARD-READ-AFTER-WRITE" in block and fg_command
               and "[nv.sl.dlss_g.tex2d.fake-swapchain-buffer]" in block)
    return layout or semaphore or fg_clear or fg_copy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--console", action="store_true", help="Check callbacks from ALL renderer restarts in qconsole")
    parser.add_argument("--instances", type=int, help="Require this many complete lifetimes in a raw renderer audit")
    args = parser.parse_args()
    text = args.log.read_text(errors="replace")
    if args.instances is not None:
        boundaries = re.findall(r"^VQ3E validation instance (begin|end)$", text, re.M)
        if args.instances < 1 or boundaries != ["begin", "end"] * args.instances:
            raise SystemExit("FAIL: raw audit does not contain every complete instance lifetime")
    blocks = diagnostic_blocks(text, args.console)
    known = unknown = 0
    for block in blocks:
        header = block.splitlines()[0]
        if known_nvidia_diagnostic(block):
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
