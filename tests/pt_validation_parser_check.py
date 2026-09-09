"""Keep restart-log parsing strict: an unrelated error must never be whitelisted."""
import unittest
from pt_validation_check import diagnostic_blocks, known_nvidia_diagnostic

PREFIX = "^3Streamline: [streamline][error]vulkan.cpp:1043[debugUtilsMessengerCallback] "
KNOWN = ("vkQueueSubmit [nv.sl.dlss_g.cmdCtx.pacer.command-buffer] "
         "[nv.sl.dlss_g.tex2d.fake-swapchain-buffer]\n"
         "The Vulkan spec states: VUID-vkCmdDraw-None-09600")


class ValidationParserTests(unittest.TestCase):
    def test_all_instances_and_unknown_error(self):
        text = PREFIX + KNOWN + "\n\nvid_restart\n" + PREFIX + "new unexpected error\n\n" + PREFIX + KNOWN
        blocks = diagnostic_blocks(text, True)
        self.assertEqual(len(blocks), 3)
        self.assertEqual([known_nvidia_diagnostic(b) for b in blocks], [True, False, True])

    def test_engine_resource_not_whitelisted(self):
        self.assertFalse(known_nvidia_diagnostic(KNOWN.replace("nv.sl.dlss_g.cmdCtx.pacer.command-buffer", "engine.command-buffer")))
        self.assertFalse(known_nvidia_diagnostic(KNOWN.replace("VUID-vkCmdDraw-None-09600", "VUID-new-error")))

    def test_exact_duplicate_notice_only(self):
        notice = "(Warning - This VUID has now been reported 2 times, which is the duplicate_message_limit value, this will be the last time reporting it)."
        self.assertEqual(diagnostic_blocks(PREFIX + notice, True), [])
        self.assertEqual(len(diagnostic_blocks(PREFIX + notice + " unexpected", True)), 1)

    def test_layer_file_still_supported(self):
        self.assertEqual(len(diagnostic_blocks("Validation Error: " + KNOWN)), 1)

    def test_teardown_leak_never_whitelisted(self):
        text = "VQ3E validation instance begin\nValidation Error: " + KNOWN + "\n"
        text += ("Validation Error: [ VUID-vkDestroyDevice-device-05137 ]\n"
                 "134 leaked objects [nv.ngx.dlssg.resource]\nVQ3E validation instance end\n")
        blocks = diagnostic_blocks(text)
        self.assertEqual([known_nvidia_diagnostic(b) for b in blocks], [True, False])

    def test_incomplete_vendor_message_not_whitelisted(self):
        # A vendor callback can omit the diagnostic header/object names.
        # Do not infer ownership from a generic synchronization explanation.
        blocks = diagnostic_blocks(PREFIX + ".\nThe current synchronization allows ...", True)
        self.assertEqual(len(blocks), 1)
        self.assertFalse(known_nvidia_diagnostic(blocks[0]))


if __name__ == "__main__":
    unittest.main()
