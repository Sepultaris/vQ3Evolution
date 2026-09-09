"""CPU byte-model and source contracts for persistent static scene prefixes.

No GPU execution: these checks do not establish Vulkan runtime correctness.
"""
from pathlib import Path
import unittest


class SceneBuffer:
    def __init__(self, capacity=256):
        self.cpu = bytearray(capacity)
        self.staging = bytearray(capacity)
        self.gpu = bytearray(capacity)
        self.load(b'')

    def load(self, world):
        self.world = world
        self.pending = True

    def capture(self, dynamic):
        if self.pending:
            self.cpu[:len(self.world)] = self.world
        self.total = len(self.world)+len(dynamic)
        self.cpu[len(self.world):self.total] = dynamic

    def upload(self):
        offset = 0 if self.pending else len(self.world)
        size = self.total-offset
        self.staging[offset:self.total] = self.cpu[offset:self.total]
        self.gpu[offset:self.total] = self.staging[offset:self.total]
        self.pending = False
        return offset, size


class SceneUpload(unittest.TestCase):
    def test_first_frame_sends_world_and_dynamic(self):
        scene = SceneBuffer()
        scene.load(b'world')
        scene.capture(b'mover')
        self.assertEqual(scene.upload(), (0, 10))
        self.assertEqual(scene.gpu[:10], b'worldmover')

    def test_later_frames_send_only_dynamic(self):
        scene = SceneBuffer()
        scene.load(b'world')
        scene.capture(b'old')
        scene.upload()
        scene.capture(b'new marks')
        self.assertEqual(scene.upload(), (5, 9))
        self.assertEqual(scene.gpu[:scene.total], b'worldnew marks')

    def test_cached_cpu_prefix_is_not_recopied(self):
        scene = SceneBuffer()
        scene.load(b'world')
        scene.capture(b'old')
        scene.upload()
        scene.world = b'xxxxx'  # Detect an accidental read of the source cache.
        scene.capture(b'new')
        scene.upload()
        self.assertEqual(scene.cpu[:5], b'world')
        self.assertEqual(scene.gpu[:5], b'world')

    def test_map_change_same_size_still_uploads_prefix(self):
        scene = SceneBuffer()
        for world in (b'world', b'other'):
            scene.load(world)
            scene.capture(b'actor')
            self.assertEqual(scene.upload(), (0, 10))
            self.assertEqual(scene.gpu[:scene.total], world+b'actor')

    def test_shrinking_map_replaces_old_prefix_and_dynamic(self):
        scene = SceneBuffer()
        for world in (b'long first map', b'map'):
            scene.load(world)
            scene.capture(b'dynamic')
            scene.upload()
            self.assertEqual(scene.gpu[:scene.total], world+b'dynamic')

    def test_static_only_frame_has_no_geometry_copy(self):
        scene = SceneBuffer()
        scene.load(b'world')
        scene.capture(b'actor')
        scene.upload()
        scene.capture(b'')
        self.assertEqual(scene.upload(), (5, 0))
        self.assertEqual(scene.gpu[:scene.total], b'world')

    def test_reference_reuses_cpu_data_but_can_upload_dynamic_suffix(self):
        scene = SceneBuffer()
        scene.load(b'world')
        scene.capture(b'frozen')
        scene.upload()
        self.assertEqual(scene.upload(), (5, 6))
        self.assertEqual(scene.gpu[:scene.total], b'worldfrozen')

    def test_empty_world_sends_all_dynamic_data(self):
        scene = SceneBuffer()
        for dynamic in (b'first', b'next'):
            scene.capture(dynamic)
            self.assertEqual(scene.upload(), (0, len(dynamic)))
            self.assertEqual(scene.gpu[:scene.total], dynamic)

    def test_native_cache_and_offset_contracts(self):
        root = Path(__file__).resolve().parents[1]/'code/renderer_vulkan'
        rt = (root/'vk_raytracing.c').read_text()
        pt = (root/'vk_pathtrace.c').read_text()
        for source, prefix in ((rt, 'rt'), (pt, 'pt')):
            self.assertIn(f'{prefix}.world_upload_pending = qtrue;', source)
            self.assertIn(f'{prefix}.world_upload_pending = qfalse;', source)
        self.assertIn('VkBufferCopy copy = {offsets[i], offsets[i], sizes[i]-offsets[i]};', rt)
        self.assertIn('offsets[i] > sizes[i] || sizes[i] > buffers[i]->size', rt)
        self.assertIn('VkBufferCopy copy = {offset, offset, size};', pt)
        self.assertIn('size > b->size-offset', pt)
        self.assertIn('pt.world_upload_pending && pt.world_vertices', pt)
        self.assertIn('rt.world_upload_pending && rt.world_vertex_count', rt)
        self.assertIn('if (pt.materials_dirty)', pt)

    def test_short_capture_waits_before_changing_view_or_quitting(self):
        config = (Path(__file__).parent/'pt_integration_short.cfg').read_text().splitlines()
        captures = [i for i, line in enumerate(config) if line.startswith('screenshotJPEG ')]
        self.assertEqual(len(captures), 3)
        for index in captures:
            self.assertEqual(config[index+1], 'wait 3')


if __name__ == '__main__':
    unittest.main()
