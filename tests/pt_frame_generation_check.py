"""Require actual generated presentations in both static and moving gameplay.

An enabled cvar, successful SDK status, or one early activation is insufficient.
Only inspect a completed isolated run of pt_frame_generation.cfg.
"""
import argparse
import re
import unittest
from pathlib import Path


STATS = re.compile(
    r"Frame Generation: (?:requested|enabled) (\d+), viewport active (\d+), "
    r"queries (\d+), presented (\d+), peak (\d+), result (-?\d+), status 0x([0-9a-fA-F]+)"
)


def check(text, neural_rendering=False):
    if 'NVIDIA focus source: Windows foreground process' not in text:
        raise ValueError('inconclusive; log lacks native Windows foreground-focus evidence')
    if "VK_PRESENT_MODE_IMMEDIATE_KHR mode" not in text:
        raise ValueError("Vulkan Frame Generation did not select unsynchronized presentation")
    if "Could not find a window corresponding to this application" in text:
        raise ValueError("Streamline did not receive the window/surface association")
    if neural_rendering and "NVIDIA DLSS Neural Rendering evaluation active" not in text:
        raise ValueError("Neural Rendering did not evaluate")
    phases = re.findall(r"^PT_FG_(BASELINE|STATIC|MOVING)$", text, re.M)
    if phases != ["BASELINE", "STATIC", "MOVING"] or "Streamline shutdown complete before device destruction" not in text:
        raise ValueError("incomplete static/moving presentation test")
    previous_queries = previous_presented = None
    previous_history = None
    for phase in phases:
        block = text.split("PT_FG_" + phase + "\n", 1)[1].split("PT_FG_", 1)[0]
        window = re.search(r"NVIDIA window state: focused (\d+), minimized (\d+)", block)
        if not window or window.groups() != ("1", "0"):
            raise ValueError(f"{phase}: inconclusive; keep the game focused and visible for the whole test")
        match = STATS.search(block)
        if not match:
            raise ValueError(f"{phase}: missing cached presentation counters")
        requested, active, queries, presented, peak, result = map(int, match.groups()[:6])
        status = int(match[7], 16)
        if (requested, active, result, status) != (1, 1, 0, 0):
            raise ValueError(f"{phase}: feature inactive or SDK failure")
        history_match = re.search(r"NVIDIA FG query history: unfocused (\d+), minimized (\d+), failed (\d+)", block)
        if not history_match:
            raise ValueError(f"{phase}: missing full-interval focus/error history")
        history = tuple(map(int, history_match.groups()))
        if phase == "BASELINE":
            previous_queries, previous_presented = queries, presented
            previous_history = history
            continue
        if history[:2] != previous_history[:2]:
            raise ValueError(f"{phase}: inconclusive; focus was lost during the measured interval")
        if history[2] != previous_history[2]:
            raise ValueError(f"{phase}: SDK failure during the measured interval")
        samples = queries - previous_queries
        extra = presented - previous_presented - samples
        # Allow startup latency and asynchronous pacing; one lucky generated
        # frame must not make an otherwise non-interpolating phase pass.
        if samples < 20 or peak < 2 or extra < samples // 4:
            raise ValueError(f"{phase}: no sustained interpolation ({extra} extra / {samples} rendered)")
        previous_queries, previous_presented = queries, presented
        previous_history = history


class CounterChecks(unittest.TestCase):
    def fixture(self, static=180, moving=380):
        return ("NVIDIA focus source: Windows foreground process\n"
                "VK_PRESENT_MODE_IMMEDIATE_KHR mode\n"
                "NVIDIA DLSS Neural Rendering evaluation active\n"
                "PT_FG_BASELINE\nFrame Generation: requested 1, viewport active 1, "
                "queries 1000, presented 1000, peak 1, result 0, status 0x0\n"
                "NVIDIA window state: focused 1, minimized 0\n"
                "NVIDIA FG query history: unfocused 950, minimized 0, failed 0\n"
                "PT_FG_STATIC\nFrame Generation: requested 1, viewport active 1, "
                f"queries 1100, presented {1000 + static}, peak 2, result 0, status 0x0\n"
                "NVIDIA window state: focused 1, minimized 0\n"
                "NVIDIA FG query history: unfocused 950, minimized 0, failed 0\n"
                "PT_FG_MOVING\nFrame Generation: requested 1, viewport active 1, "
                f"queries 1200, presented {1000 + moving}, peak 2, result 0, status 0x0\n"
                "NVIDIA window state: focused 1, minimized 0\n"
                "NVIDIA FG query history: unfocused 950, minimized 0, failed 0\n"
                "Streamline shutdown complete before device destruction\n")

    def test_interpolation_in_both_phases(self):
        check(self.fixture(), True)

    def test_enabled_but_only_real_frames(self):
        with self.assertRaisesRegex(ValueError, "STATIC: no sustained"):
            check(self.fixture(100, 200))

    def test_movement_stops_interpolation(self):
        with self.assertRaisesRegex(ValueError, "MOVING: no sustained"):
            check(self.fixture(180, 280))

    def test_sdk_error(self):
        with self.assertRaisesRegex(ValueError, "SDK failure"):
            check(self.fixture().replace("status 0x0", "status 0x2"))

    def test_incomplete_run(self):
        with self.assertRaisesRegex(ValueError, "incomplete"):
            check(self.fixture().replace("Streamline shutdown complete", "crash"))

    def test_surface_routing(self):
        with self.assertRaisesRegex(ValueError, "window/surface"):
            check(self.fixture() + "Could not find a window corresponding to this application")

    def test_unfocused_is_inconclusive(self):
        with self.assertRaisesRegex(ValueError, "inconclusive"):
            check(self.fixture(100, 200).replace("focused 1", "focused 0"))

    def test_mid_interval_focus_loss(self):
        with self.assertRaisesRegex(ValueError, "focus was lost"):
            check(self.fixture().replace("unfocused 950", "unfocused 940", 1))

    def test_mid_interval_sdk_error(self):
        with self.assertRaisesRegex(ValueError, "SDK failure during"):
            check(self.fixture().replace("failed 0", "failed 1", 1))

    def test_old_logs_cannot_claim_full_interval_focus(self):
        with self.assertRaisesRegex(ValueError, "missing full-interval"):
            check(re.sub(r"NVIDIA FG query history:.*\n", "", self.fixture()))

    def test_sdl_focus_alone_is_insufficient(self):
        with self.assertRaisesRegex(ValueError, 'native Windows'):
            check(self.fixture().replace('NVIDIA focus source: Windows foreground process', ''))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path, nargs="?")
    parser.add_argument("--neural-rendering", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        unittest.main(argv=[__file__])
    elif args.log:
        try:
            check(args.log.read_text(errors="replace"), args.neural_rendering)
        except ValueError as error:
            raise SystemExit(f"FAIL: {error}")
        print("PASS: sustained generated presentations during static and moving gameplay")
    else:
        parser.error("provide a completed log or --self-test")
