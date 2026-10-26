#!/usr/bin/env python3
"""Builds a small resources.arsc the way aapt2 lays it out.

The result is used as a fixture by tests/test_arsc_encode.sh: the encoder has
to reproduce it byte for byte, so the layout here follows the AOSP rules
(4 byte aligned pools, 288 byte package header, 20 + sizeof(config) type
chunks, entries stored back to back).

Usage: arsc_build.py <out.arsc>
"""
import struct
import sys

RES_STRING_POOL = 0x0001
RES_TABLE = 0x0002
RES_PACKAGE = 0x0200
RES_TYPE = 0x0201
RES_TYPE_SPEC = 0x0202

PACKAGE_HEADER = 288
CONFIG_SIZE = 64


def string_pool(strings, utf8):
    """Builds a ResStringPool chunk."""
    offsets = []
    data = bytearray()
    for s in strings:
        offsets.append(len(data))
        if utf8:
            raw = s.encode('utf-8')
            assert len(s) < 0x80 and len(raw) < 0x80, 'fixture strings are ASCII'
            data += bytes([len(s), len(raw)]) + raw + b'\x00'
        else:
            data += struct.pack('<H', len(s)) + s.encode('utf-16-le') + b'\x00\x00'
    while len(data) % 4:
        data += b'\x00'
    header = 28
    size = header + 4 * len(strings) + len(data)
    out = struct.pack('<HHI', RES_STRING_POOL, header, size)
    out += struct.pack('<IIIII', len(strings), 0, 0x100 if utf8 else 0,
                       header + 4 * len(strings), 0)
    for o in offsets:
        out += struct.pack('<I', o)
    return out + bytes(data)


def value(data_type, data):
    """A Res_value."""
    return struct.pack('<HBBi', 8, 0, data_type, data)


def simple_entry(key, data_type, data):
    """A ResTable_entry followed by a Res_value."""
    return struct.pack('<HHI', 8, 0, key) + value(data_type, data)


def complex_entry(key, items):
    """A ResTable_map_entry followed by its ResTable_map items."""
    out = struct.pack('<HHIII', 16, 1, key, 0, len(items))
    for name, vtype, vdata in items:
        out += struct.pack('<I', name) + value(vtype, vdata)
    return out


def type_spec(type_id, entry_count, res1=0):
    size = 16 + 4 * entry_count
    out = struct.pack('<HHI', RES_TYPE_SPEC, 16, size)
    out += struct.pack('<BBHI', type_id, 0, res1, entry_count)
    out += b'\x00' * (4 * entry_count)
    return out


def type_chunk(type_id, entries, config=None):
    """A ResTable_type chunk with the entries stored back to back."""
    config = config if config is not None else default_config()
    header = 20 + len(config)
    body = b''
    offsets = []
    for e in entries:
        offsets.append(len(body))
        body += e
    offsets += [0xFFFFFFFF] * 0
    entry_count = len(entries)
    size = header + 4 * entry_count + len(body)
    out = struct.pack('<HHI', RES_TYPE, header, size)
    out += struct.pack('<BBHII', type_id, 0, 0, entry_count, header + 4 * entry_count)
    out += config
    for o in offsets:
        out += struct.pack('<I', o)
    return out + body


def default_config():
    cfg = bytearray(CONFIG_SIZE)
    struct.pack_into('<I', cfg, 0, CONFIG_SIZE)
    return bytes(cfg)


def night_config():
    cfg = bytearray(default_config())
    # uiMode sits at offset 40, UI_MODE_NIGHT_YES is 0x10
    struct.pack_into('<I', cfg, 40, 0x10)
    return bytes(cfg)


def build():
    values = ['Hello Agent', 'Second string', 'Night value']
    type_names = ['string']
    key_names = ['greeting', 'title', 'night']

    global_pool = string_pool(values, True)
    type_pool = string_pool(type_names, False)
    key_pool = string_pool(key_names, True)

    # type 1: three entries, one configuration plus a night variant.
    day = [
        simple_entry(0, 0x03, 0),    # greeting -> "Hello Agent"
        simple_entry(1, 0x03, 1),    # title   -> "Second string"
        simple_entry(2, 0x10, 42),   # night   -> integer
    ]
    night = [
        simple_entry(0, 0x03, 2),    # greeting -> "Night value"
        simple_entry(1, 0x03, 1),
        simple_entry(2, 0x10, 7),
    ]
    # type 2: a complex (map) entry.
    style = [
        complex_entry(0, [(0x01010098, 0x10, 1), (0x0101009a, 0x10, 0)]),
    ]

    chunks = bytearray()
    chunks += type_spec(1, len(day), res1=0x0002)
    chunks += type_chunk(1, day)
    chunks += type_chunk(1, night, night_config())
    chunks += type_spec(2, len(style))
    chunks += type_chunk(2, style)

    package = bytearray()
    package += struct.pack('<HHI', RES_PACKAGE, PACKAGE_HEADER,
                            PACKAGE_HEADER + len(type_pool) + len(key_pool) + len(chunks))
    package += struct.pack('<I', 0x7F)
    package += 'com.example.fixture'.encode('utf-16-le').ljust(256, b'\x00')
    package += struct.pack('<IIII', PACKAGE_HEADER, 0, PACKAGE_HEADER + len(type_pool), 0)
    package += struct.pack('<I', 0)
    package += type_pool
    package += key_pool
    package += chunks

    table = struct.pack('<HHII', RES_TABLE, 12, 12 + len(global_pool) + len(package), 1)
    table += global_pool
    table += package
    return table


def main():
    if len(sys.argv) < 2:
        print('usage: arsc_build.py <out.arsc>', file=sys.stderr)
        return 1
    data = build()
    open(sys.argv[1], 'wb').write(data)
    return 0


if __name__ == '__main__':
    sys.exit(main())
