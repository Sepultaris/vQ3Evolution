#!/usr/bin/env python3
"""Execute the patched QVM arithmetic and verify archive replacement safety."""
import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('tc_patch', ROOT/'tools/patch-truecombat-ui.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


class ArithmeticVM:
    """Small executor for the actual instruction blocks, not a duplicate formula."""
    def __init__(self, data, code):
        self.memory = bytearray(1048576)
        header, _ = patch.decode(data)
        self.memory[:len(data)-header[4]] = data[header[4]:]
        self.code, self.stack, self.sp, self.cache_calls = code, [], 600000, 0

    def store(self, address, value):
        struct.pack_into('<I', self.memory, address, value & 0xffffffff)

    def load(self, address):
        return struct.unpack_from('<i', self.memory, address)[0]

    def store_float(self, address, value):
        struct.pack_into('<f', self.memory, address, value)

    def load_float(self, address):
        return struct.unpack_from('<f', self.memory, address)[0]

    @staticmethod
    def bits(value):
        return struct.unpack('<i', struct.pack('<f', value))[0]

    @staticmethod
    def number(bits):
        return struct.unpack('<f', struct.pack('<I', bits & 0xffffffff))[0]

    def run(self, pc, stop=None):
        for _ in range(1000):
            if pc == stop:
                return
            op, arg = self.code[pc]
            pc += 1
            s = self.stack
            if op == 'CONST': s.append(arg)
            elif op == 'LOCAL': s.append(self.sp + arg)
            elif op == 'ENTER': self.sp -= arg
            elif op == 'LEAVE': self.sp += arg; return
            elif op == 'LOAD4': s.append(self.load(s.pop()))
            elif op == 'STORE4': value, address = s.pop(), s.pop(); self.store(address, value)
            elif op == 'CVIF': s.append(self.bits(float(s.pop())))
            elif op in ('ADDF', 'SUBF', 'MULF', 'DIVF'):
                b, a = self.number(s.pop()), self.number(s.pop())
                s.append(self.bits({'ADDF': lambda: a+b, 'SUBF': lambda: a-b,
                                    'MULF': lambda: a*b, 'DIVF': lambda: a/b}[op]()))
            elif op == 'MULI': b, a = s.pop(), s.pop(); s.append(a*b)
            elif op == 'LEI':
                b, a = s.pop(), s.pop()
                if a <= b: pc = arg
            elif op == 'JUMP': pc = s.pop()
            elif op == 'POP': s.pop()
            elif op == 'PUSH': s.append(0)
            elif op == 'CALL':
                target = s.pop()
                if target != patch.CACHE: raise AssertionError(('unexpected call', target))
                self.cache_calls += 1
                s.append(0)
            else: raise AssertionError(('unexpected opcode', op))
        raise AssertionError('Instruction limit exceeded')

    def initialize(self, width, height, helper=None):
        self.store(patch.VID_WIDTH, width)
        self.store(patch.VID_HEIGHT, height)
        self.run(5730, 5771)
        assert not self.stack
        if helper is not None:
            self.run(helper)
            assert self.cache_calls == 1
            self.stack.clear()

    def rectangle(self, x, y, width, height):
        for i, value in enumerate((x, y, width, height)):
            address = 700000 + i*4
            self.store(self.sp + 8 + i*4, address)
            self.store_float(address, value)
        self.run(5782)
        self.stack.clear()
        return tuple(self.load_float(700000 + i*4) for i in range(4))


class Tests(unittest.TestCase):
    def test_01_exact_patch_and_layout(self):
        self.assertEqual(patch.sha256(ORIGINAL), patch.ORIGINAL_SHA256)
        self.assertEqual(patch.sha256(PATCHED), patch.PATCHED_SHA256)
        self.assertEqual(patch.patch_qvm(PATCHED), PATCHED)
        old_header, old_code = patch.decode(ORIGINAL)
        new_header, new_code = patch.decode(PATCHED)
        self.assertEqual(ORIGINAL[old_header[4]:], PATCHED[new_header[4]:])
        self.assertEqual(new_header[5:], old_header[5:])
        self.assertEqual(len(new_code), len(old_code) + 19)
        allowed = {5771, 5794, 5795, 5796, 5797}
        self.assertEqual({i for i in range(len(old_code)) if old_code[i] != new_code[i]}, allowed)
        # Every original instruction number/jump target remains valid and stable.
        for i, (op, arg) in enumerate(old_code):
            if op in patch.OPS[11:27]: self.assertEqual(new_code[i], (op, arg))

    def test_02_actual_qvm_geometry(self):
        _, old = patch.decode(ORIGINAL)
        _, new = patch.decode(PATCHED)
        for width, height in ((640,480), (960,720), (1920,1080), (2560,1080),
                              (3440,1440), (5120,1440), (720,1280)):
            with self.subTest(resolution=(width,height)):
                vm = ArithmeticVM(PATCHED, new)
                vm.initialize(width, height, len(old))
                scale = min(width/640, height/480)
                bias = max(0, (width-height*4/3)/2)
                self.assertAlmostEqual(vm.load_float(patch.XSCALE), scale, places=5)
                self.assertAlmostEqual(vm.load_float(patch.BIAS), bias, places=3)
                for x in (-16, 0, 48, 184, 304, 320, 456, 624, 640):
                    rect = vm.rectangle(x, 80, 128, 96)
                    expected = (x*scale+bias, 80*height/480, 128*scale, 96*height/480)
                    for actual, want in zip(rect, expected): self.assertAlmostEqual(actual, want, places=3)
                for x in (48,184,320,456):
                    left, _, w, _ = vm.rectangle(x,80,128,96)
                    self.assertGreaterEqual(left, 0)
                    self.assertLessEqual(left+w, width)
                if width*480 <= height*640:
                    original_vm = ArithmeticVM(ORIGINAL, old)
                    original_vm.initialize(width,height)
                    for x in (0,48,184,320,456,640):
                        self.assertEqual(vm.rectangle(x,80,128,96), original_vm.rectangle(x,80,128,96))

    def test_03_unknown_versions_rejected(self):
        altered = bytearray(ORIGINAL)
        altered[-1] ^= 1
        for unsupported in (bytes(altered), b'', b'not a QVM'):
            with self.assertRaisesRegex(ValueError, 'Unsupported True Combat'):
                patch.patch_qvm(unsupported)

    def test_04_archive_backup_and_idempotence(self):
        with tempfile.TemporaryDirectory(prefix='tc-ui-check-') as directory:
            archive = Path(directory)/'pak6.pk3'
            with zipfile.ZipFile(archive, 'w') as package:
                package.comment = b'Archive comment'
                for name, data in ((patch.UI_ENTRY, ORIGINAL), ('vm/cgame.qvm', b'unchanged game'),
                                   ('textures/test.tga', b'unchanged asset')):
                    info = zipfile.ZipInfo(name, (2001,12,28,12,0,0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.comment = b'Entry comment'
                    package.writestr(info, data)
            original_archive = archive.read_bytes()
            backup = patch.apply_archive(archive)
            self.assertEqual(backup.read_bytes(), original_archive)
            self.assertEqual(patch.read_ui(archive), PATCHED)
            patch.verify_archive(backup, archive, PATCHED)
            first_patch = archive.read_bytes()
            self.assertIsNone(patch.apply_archive(archive))
            self.assertEqual(archive.read_bytes(), first_patch)
            self.assertEqual(backup.read_bytes(), original_archive)
            self.assertFalse(list(archive.parent.glob('.tc-ui-*')))
            # Refuse to destroy an existing, different backup.
            archive.write_bytes(original_archive)
            backup.write_bytes(b'other backup')
            with self.assertRaisesRegex(ValueError, 'Existing backup differs'):
                patch.apply_archive(archive)
            self.assertEqual(archive.read_bytes(), original_archive)
            self.assertEqual(backup.read_bytes(), b'other backup')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='Original mod archive or its backup')
    args = parser.parse_args()
    ORIGINAL = patch.read_ui(args.archive)
    PATCHED = patch.patch_qvm(ORIGINAL)
    unittest.main(argv=['truecombat_ui_patch_check'], verbosity=2)
