#!/usr/bin/env python3
"""Dump a resources.arsc into a canonical text form.

The dump is independent of the C implementation so it can be used as an
oracle: the re-encoded table must produce the same dump as the original.

Usage: arsc_dump.py <resources.arsc> [out.txt]
"""
import struct
import sys

RES_STRING_POOL_TYPE = 0x0001
RES_TABLE_TYPE = 0x0002
RES_TABLE_PACKAGE_TYPE = 0x0200
RES_TABLE_TYPE_TYPE = 0x0201
RES_TABLE_TYPE_SPEC_TYPE = 0x0202


def u16(d, o):
    return struct.unpack_from('<H', d, o)[0]


def u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def pool_strings(pool):
    count = u32(pool, 8)
    flags = u32(pool, 16)
    strs_start = u32(pool, 20)
    hdr = u16(pool, 2)
    out = []
    for i in range(count):
        off = u32(pool, hdr + i * 4)
        p = strs_start + off
        if flags & 0x100:
            if pool[p] & 0x80:
                p += 2
            else:
                p += 1
            if pool[p] & 0x80:
                blen = ((pool[p] & 0x7F) << 8) | pool[p + 1]
                p += 2
            else:
                blen = pool[p]
                p += 1
            out.append(pool[p:p + blen].decode('utf-8', 'replace'))
        else:
            ln = u16(pool, p)
            p += 2
            out.append(pool[p:p + ln * 2].decode('utf-16-le', 'replace'))
    return out


def dump(data, out):
    out.append('table packageCount=%d' % u32(data, 8))
    off = u16(data, 2)
    while off + 8 <= len(data):
        ctype = u16(data, off)
        chdr = u16(data, off + 2)
        csize = u32(data, off + 4)
        if csize < 8 or off + csize > len(data):
            break
        if ctype == RES_STRING_POOL_TYPE:
            values = pool_strings(data[off:off + csize])
            out.append('globalPool count=%d' % len(values))
            for i, v in enumerate(values):
                out.append('  value %d = %r' % (i, v))
        elif ctype == RES_TABLE_PACKAGE_TYPE:
            pkg_id = data[off + 8]
            name = data[off + 12:off + 268].decode('utf-16-le').split('\x00')[0]
            ts = u32(data, off + 268)
            ks = u32(data, off + 276)
            types = pool_strings(data[off + ts:off + ts + u32(data, off + ts + 4)])
            keys = pool_strings(data[off + ks:off + ks + u32(data, off + ks + 4)])
            out.append('package id=0x%02x name=%s types=%r' % (pkg_id, name, types))
            poff = off + chdr
            pkg_end = off + csize
            while poff + 8 <= pkg_end:
                ptype = u16(data, poff)
                phdr = u16(data, poff + 2)
                psize = u32(data, poff + 4)
                if psize < 8 or poff + psize > pkg_end:
                    break
                if ptype == RES_TABLE_TYPE_SPEC_TYPE:
                    tid = data[poff + 8]
                    ec = u32(data, poff + 12)
                    flags = [u32(data, poff + 16 + i * 4) for i in range(ec)]
                    out.append('  spec id=%d count=%d flags=%r' % (tid, ec, flags))
                elif ptype == RES_TABLE_TYPE_TYPE:
                    tid = data[poff + 8]
                    tflags = data[poff + 9]
                    ec = u32(data, poff + 12)
                    es = u32(data, poff + 16)
                    cfg = data[poff + 20:poff + phdr]
                    offs = [u32(data, poff + phdr + i * 4) for i in range(ec)]
                    base = poff + es
                    out.append('  type id=%d flags=%d count=%d config=%s' %
                               (tid, tflags, ec, cfg.hex()))
                    for i, o in enumerate(offs):
                        if o == 0xFFFFFFFF:
                            continue
                        e = base + o
                        esz, eflags = u16(data, e), u16(data, e + 2)
                        key = u32(data, e + 4)
                        if eflags & 1:
                            parent = u32(data, e + 8)
                            mc = u32(data, e + 12)
                            items = []
                            for j in range(mc):
                                m = e + 16 + j * 12
                                items.append((u32(data, m), data[m + 7], u32(data, m + 8)))
                            out.append('    entry %d key=%r flags=%d size=%d parent=%d map=%r' %
                                       (i, keys[key], eflags, esz, parent, items))
                        else:
                            vsize = u16(data, e + 8)
                            dtype = data[e + 11]
                            val = u32(data, e + 12)
                            out.append('    entry %d key=%r flags=%d size=%d value=%d data=%d' %
                                       (i, keys[key], eflags, esz, dtype, val))
                poff += psize
        off += csize


def main():
    path = sys.argv[1]
    data = open(path, 'rb').read()
    out = []
    dump(data, out)
    text = '\n'.join(out)
    if len(sys.argv) > 2:
        open(sys.argv[2], 'w').write(text + '\n')
    else:
        print(text)


if __name__ == '__main__':
    main()
