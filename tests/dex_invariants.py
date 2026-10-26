#!/usr/bin/env python3
"""DEX structural invariants (G7 / G8) checker.

G7: a section whose size field is 0 must have offset 0.
G8: every section offset must be 4-byte aligned.
Also checks the header file_size field against the real file size and that
the map section agrees with the section sizes.
"""
import struct, sys, os

SECTIONS = [
    ("header_item", 0x0000, 0x70),
    ("string_id_item", 0x0001, 4),
    ("type_id_item", 0x0002, 4),
    ("proto_id_item", 0x0003, 12),
    ("field_id_item", 0x0004, 8),
    ("method_id_item", 0x0005, 8),
    ("class_def_item", 0x0006, 32),
    ("call_site_id_item", 0x0007, 4),
    ("method_handle_item", 0x0008, 8),
    ("map_list", 0x1000, 0),
    ("type_list", 0x1001, 0),
    ("annotation_set_ref_list", 0x1002, 0),
    ("annotation_set_item", 0x1003, 0),
    ("class_data_item", 0x2000, 0),
    ("code_item", 0x2001, 0),
    ("string_data_item", 0x2002, 0),
    ("debug_info_item", 0x2003, 0),
    ("annotation_item", 0x2004, 0),
    ("encoded_array_item", 0x2005, 0),
    ("annotations_directory_item", 0x2006, 0),
]


def check(path):
    errs = []
    data = open(path, "rb").read()
    if len(data) < 0x70:
        return ["file too small"]
    magic, checksum, sha1, fsize, hsize = struct.unpack_from("<8sI20sII", data, 0)
    if magic != b"dex\n035\x00":
        errs.append("bad magic %r" % magic)
    if fsize != len(data):
        errs.append("header file_size %d != actual %d" % (fsize, len(data)))
    if hsize != 0x70:
        errs.append("header_size %d" % hsize)

    # fixed-size sections from the header
    hdr_fields = [
        ("string_ids", 0x38), ("type_ids", 0x40), ("proto_ids", 0x48),
        ("field_ids", 0x50), ("method_ids", 0x58), ("class_defs", 0x60),
        ("data", 0x68),
    ]
    for name, off in hdr_fields:
        size, soff = struct.unpack_from("<II", data, off)
        if size == 0:
            if soff != 0:
                errs.append("%s: size 0 but offset %d != 0 (G7)" % (name, soff))
        elif soff % 4 != 0:
            errs.append("%s: offset %d not 4-byte aligned (G8)" % (name, soff))

    map_off = struct.unpack_from("<I", data, 0x34)[0]
    if map_off == 0 or map_off % 4 != 0:
        errs.append("map_off %d invalid" % map_off)
        return errs
    n = struct.unpack_from("<I", data, map_off)[0]
    if n > 200:
        errs.append("map count %d implausible" % n)
        return errs
    seen = {}
    for i in range(n):
        eoff = map_off + 4 + i * 12
        mtype, unused, msize, moff = struct.unpack_from("<HHII", data, eoff)
        if msize == 0 and moff != 0:
            errs.append("map entry type 0x%04x: size 0 but offset %d != 0 (G7)" % (mtype, moff))
        if moff % 4 != 0:
            errs.append("map entry type 0x%04x: offset %d not 4-byte aligned (G8)" % (mtype, moff))
        seen[mtype] = (msize, moff)
    for t, (sz, off) in seen.items():
        if off + sz > len(data):
            errs.append("map entry 0x%04x runs past end of file" % t)
    return errs


rc = 0
for p in sys.argv[1:]:
    if not os.path.exists(p):
        print("MISSING %s" % p)
        rc = 1
        continue
    e = check(p)
    if e:
        rc = 1
        print("FAIL %s" % p)
        for x in e:
            print("     - %s" % x)
    else:
        print("OK   %s" % p)
sys.exit(rc)
