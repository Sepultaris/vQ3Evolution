#!/usr/bin/env python3
"""Patch the installed True Combat 0.45 build-12 UI, never its gameplay VMs.

Default is inspection only. --apply atomically replaces vm/ui.qvm in the
existing archive after saving a byte-identical backup outside the .pk3 suffix.
No copyrighted mod payload is distributed with this tool.
"""
import argparse
import copy
import hashlib
import os
from pathlib import Path
import struct
import tempfile
import zipfile

ORIGINAL_SHA256 = '46427997abbd31ebdc5380f31bceffa83690f10c05f46e43f855a7b470c4ca41'
PATCHED_SHA256 = '0856a770866faeca514308baa3e0ab20b77e894ac82e763a44f3cf4be7aaaa73'
UI_ENTRY = 'vm/ui.qvm'
OPS = ("UNDEF IGNORE BREAK ENTER LEAVE CALL PUSH POP CONST LOCAL JUMP "
       "EQ NE LTI LEI GTI GEI LTU LEU GTU GEU EQF NEF LTF LEF GTF GEF "
       "LOAD1 LOAD2 LOAD4 STORE1 STORE2 STORE4 ARG BLOCK_COPY SEX8 SEX16 "
       "NEGI ADD SUB DIVI DIVU MODI MODU MULI MULU BAND BOR BXOR BCOM "
       "LSH RSHI RSHU NEGF ADDF SUBF DIVF MULF CVIF CVFI").split()
IMM32 = set(OPS[11:27]) | {'ENTER', 'LEAVE', 'CONST', 'LOCAL', 'BLOCK_COPY'}
XSCALE, YSCALE, BIAS = 242400, 242404, 242412
VID_WIDTH, VID_HEIGHT = 242316, 242320
CACHE = 39525


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def decode(data):
    if len(data) < 32:
        raise ValueError('Truncated QVM header')
    header = list(struct.unpack_from('<8I', data))
    magic, count, start, length, data_start, data_len, lit_len, _ = header
    if (magic != 0x12721444 or start != 32 or data_start != start + length
            or data_start + data_len + lit_len != len(data)):
        raise ValueError('Unsupported QVM layout')
    instructions, offset = [], start
    for _ in range(count):
        if offset >= data_start or data[offset] >= len(OPS):
            raise ValueError('Invalid QVM instruction')
        op, arg = OPS[data[offset]], None
        offset += 1
        size = 4 if op in IMM32 else 1 if op == 'ARG' else 0
        if offset + size > data_start:
            raise ValueError('Truncated QVM operand')
        if size == 4:
            arg = struct.unpack_from('<i', data, offset)[0]
        elif size == 1:
            arg = data[offset]
        offset += size
        instructions.append((op, arg))
    if any(data[offset:data_start]) or data_start - offset > 3:
        raise ValueError('Unexpected code padding')
    return header, instructions


def encode(instructions):
    code = bytearray()
    for op, arg in instructions:
        code.append(OPS.index(op))
        if op in IMM32:
            code.extend(struct.pack('<i', arg))
        elif op == 'ARG':
            code.append(arg)
        elif arg is not None:
            raise ValueError('Unexpected instruction operand')
    code.extend(bytes((-len(code)) % 4))
    return bytes(code)


def patch_qvm(original):
    digest = sha256(original)
    if PATCHED_SHA256 and digest == PATCHED_SHA256:
        return original
    if digest != ORIGINAL_SHA256:
        raise ValueError('Unsupported True Combat UI version; no changes made')
    header, instructions = decode(original)
    if header[1] != 96504 or instructions[5771:5774] != [
            ('CONST', CACHE), ('CALL', None), ('POP', None)]:
        raise ValueError('UI initialization does not match the audited version')
    expected = [('CONST', XSCALE), ('LOAD4', None), ('CONST', BIAS),
                ('LOAD4', None), ('ADDF', None), ('MULF', None), ('STORE4', None)]
    if instructions[5792:5799] != expected:
        raise ValueError('UI coordinate helper does not match the audited version')

    # Correct x * (scale + bias) to x * scale + bias. Instruction count and
    # total encoded size of this block stay unchanged: no old jump relocations.
    instructions[5792:5799] = [
        ('CONST', XSCALE), ('LOAD4', None), ('MULF', None),
        ('CONST', BIAS), ('LOAD4', None), ('ADDF', None), ('STORE4', None)]

    # Original init calculates a centered widescreen bias but forgets to set
    # xscale=yscale. Append a small init helper, preserving ALL old instruction
    # numbers, function pointers and jump-table data. Narrow/4:3 init is unchanged.
    # Source equivalent, before the original Menu_Cache():
    # if (480 * vidWidth > 640 * vidHeight) uis.xscale = uis.yscale;
    helper = len(instructions)
    instructions[5771] = ('CONST', helper)
    instructions += [
        ('ENTER', 8), ('CONST', VID_WIDTH), ('LOAD4', None), ('CONST', 480),
        ('MULI', None), ('CONST', VID_HEIGHT), ('LOAD4', None), ('CONST', 640),
        ('MULI', None), ('LEI', helper + 14),
        ('CONST', XSCALE), ('CONST', YSCALE), ('LOAD4', None), ('STORE4', None),
        ('CONST', CACHE), ('CALL', None), ('POP', None), ('PUSH', None), ('LEAVE', 8)]
    code = encode(instructions)
    payload = original[header[4]:]
    header[1], header[3], header[4] = len(instructions), len(code), 32 + len(code)
    patched = struct.pack('<8I', *header) + code + payload
    if PATCHED_SHA256 and sha256(patched) != PATCHED_SHA256:
        raise ValueError('Internal patch checksum mismatch')
    decode(patched)
    return patched


def read_ui(archive):
    with zipfile.ZipFile(archive) as package:
        names = package.namelist()
        if len(set(n.lower() for n in names)) != len(names) or UI_ENTRY not in names:
            raise ValueError('Ambiguous/missing UI archive entry')
        return package.read(UI_ENTRY)


def verify_archive(original, candidate, patched_ui):
    with zipfile.ZipFile(original) as before, zipfile.ZipFile(candidate) as after:
        if before.namelist() != after.namelist() or before.comment != after.comment:
            raise ValueError('Archive directory changed unexpectedly')
        for a, b in zip(before.infolist(), after.infolist()):
            old, new = before.read(a), after.read(b)
            if new != (patched_ui if a.filename == UI_ENTRY else old):
                raise ValueError('Unexpected changed entry: ' + a.filename)
            for field in ('filename', 'date_time', 'compress_type', 'comment',
                          'extra', 'internal_attr', 'external_attr', 'create_system'):
                if getattr(a, field) != getattr(b, field):
                    raise ValueError('Archive metadata changed: ' + a.filename)


def apply_archive(archive):
    archive = Path(archive).resolve(strict=True)
    if not archive.is_file() or archive.suffix.lower() != '.pk3':
        raise ValueError('Expected an existing mod .pk3 archive')
    original = read_ui(archive)
    patched = patch_qvm(original)
    if patched == original:
        return None  # Already patched; do not rewrite the archive or backup.
    archive_bytes = archive.read_bytes()
    backup = archive.with_name(archive.name + '.pre-widescreen.bak')
    if backup.exists():
        if backup.read_bytes() != archive_bytes:
            raise ValueError('Existing backup differs; refusing to overwrite it')
    else:
        with backup.open('xb') as stream:
            stream.write(archive_bytes)
            stream.flush()
            os.fsync(stream.fileno())
    fd, name = tempfile.mkstemp(prefix='.tc-ui-', suffix='.tmp', dir=archive.parent)
    os.close(fd)
    temporary = Path(name)
    try:
        with zipfile.ZipFile(archive) as before, zipfile.ZipFile(temporary, 'w') as after:
            after.comment = before.comment
            for entry in before.infolist():
                after.writestr(copy.copy(entry), patched if entry.filename == UI_ENTRY
                               else before.read(entry))
        verify_archive(backup, temporary, patched)
        with temporary.open('r+b') as stream:
            os.fsync(stream.fileno())
        if archive.read_bytes() != archive_bytes:
            raise ValueError('Archive changed during patching; refusing replacement')
        os.replace(temporary, archive)
    finally:
        temporary.unlink(missing_ok=True)
    return backup


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path, help='Installed q3tc045/pak6.pk3')
    parser.add_argument('--apply', action='store_true', help='Close the game first; save backup and patch')
    args = parser.parse_args()
    try:
        original = read_ui(args.archive)
        patched = patch_qvm(original)
        print('Current UI SHA256:', sha256(original))
        print('Patched UI SHA256:', sha256(patched))
        if patched == original:
            print('Already patched; no changes.')
        elif args.apply:
            print('Patched only vm/ui.qvm. Original archive backup:', apply_archive(args.archive))
        else:
            print('Verified supported UI; inspection only. Pass --apply to install.')
    except (ValueError, OSError, zipfile.BadZipFile, KeyError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
