#!/usr/bin/env python3
"""Prints the resource table dump lines that mention a given key.

Used by tests/test_arsc_compile_xml.sh to prove that editing a value in a
qualified res directory changes that configuration and leaves the default one
alone: the compiler's output is dumped, the lines naming the key are compared
with the ones from the original table, and the first of them (the default
configuration, which aapt2 writes first) has to stay identical.

Usage: arsc_xml_probe.py entries <resources.arsc> <key>
"""
import os
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 4 or sys.argv[1] != 'entries':
        print('usage: arsc_xml_probe.py entries <resources.arsc> <key>', file=sys.stderr)
        return 2
    arsc, key = sys.argv[2], sys.argv[3]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as tmp:
        out = tmp.name
    try:
        subprocess.run(['python3', os.path.join(root, 'tests/arsc_dump.py'), arsc, out],
                       check=False, capture_output=True)
        if not os.path.exists(out):
            return 1
        with open(out, encoding='utf-8', errors='replace') as f:
            for line in f:
                if "key='%s'" % key in line:
                    sys.stdout.write(line)
    finally:
        if os.path.exists(out):
            os.remove(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
