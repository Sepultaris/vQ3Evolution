"""Negative controls: the recovery gate must reject partial/unsafe test logs."""
import contextlib
import io
import unittest
from pt_video_recovery_check import CASES, check


def fixture():
    lines = ["Destroy logical device"]
    for i, case in enumerate(CASES):
        lines += [f"PT_RECOVERY_BEGIN_{i}", f"Vulkan recovery TEST armed: {case}",
                  f"Vulkan recovery TEST {case.split('-')[0]} result: test"]
        if "timeout" not in case and "not-ready" not in case:
            lines += ["Vulkan: deferred swapchain recovery (test)",
                      "Video recovery: restarting at engine frame boundary",
                      "RE_Shutdown( 1 )", "Destroy logical device",
                      "NVIDIA DLSS Neural Rendering evaluation active",
                      "NVIDIA DLSS Frame Generation active (2 frames presented)"]
        lines += ["Path tracer: active 1, frame 118", f"PT_RECOVERY_END_{i}"]
    return "\n".join(lines + ["PT_RECOVERY_COMPLETE", "SDL audio shut down.", ""])


class RecoveryParserTests(unittest.TestCase):
    def gate(self, text):
        with contextlib.redirect_stdout(io.StringIO()):
            check(text, nvidia=True)

    def test_complete(self):
        self.gate(fixture())

    def test_missing_shutdown(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("SDL audio shut down.", ""))

    def test_missing_case(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("PT_RECOVERY_END_7", ""))

    def test_unconsumed_injection(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("Vulkan recovery TEST acquire result:", "missing:", 1))

    def test_reentrant_shutdown(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace(
                "Video recovery: restarting at engine frame boundary\nRE_Shutdown( 1 )",
                "RE_Shutdown( 1 )\nVideo recovery: restarting at engine frame boundary", 1))

    def test_retry_causes_restart(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("PT_RECOVERY_BEGIN_2\n", "PT_RECOVERY_BEGIN_2\n"
                "Video recovery: restarting at engine frame boundary\n"))

    def test_no_live_frames(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("active 1, frame 118", "active 1, frame 0", 1))

    def test_no_generated_frames(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("active (2 frames presented)", "enabled", 1))

    def test_incomplete_teardown(self):
        with self.assertRaises(ValueError):
            self.gate(fixture().replace("Destroy logical device", "", 1))


if __name__ == "__main__":
    unittest.main()
