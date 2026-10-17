#!/usr/bin/env python3
"""Extract an md::Device state blob from a Gearmulator MD/MM standalone settings file.

usage: extract_device_state.py "<Gearmulator MM.settings>" device-state.bin

The standalone stores the plugin state as JUCE MemoryBlock base64 in the filterState value:
"<size>." followed by 6-bit groups, least significant bit first, over the alphabet
".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+". The decoded processor blob is
"DSP56300" (u32-length-prefixed) + u32 version + u32-length-prefixed chunk stream of
(4-char id, u32 version, u32 length, data). The "MIDI" chunk holds u32 length + synthLib::Plugin
state, whose first two bytes (state version, state type) precede the md::Device state ("MDST").
Read the settings file only; never write to it.
"""
import re
import struct
import sys

ALPHABET = ".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"


def decode_memory_block(value):
    size_text, encoded = value.split('.', 1)
    size = int(size_text)
    lookup = {c: i for i, c in enumerate(ALPHABET)}
    out = bytearray(size)
    pos = 0
    for ch in encoded:
        bits = lookup[ch]
        for b in range(6):
            if bits & (1 << b):
                byte = pos >> 3
                if byte < size:
                    out[byte] |= 1 << (pos & 7)
            pos += 1
    return bytes(out)


def device_state(blob):
    n = struct.unpack_from('<I', blob, 0)[0]
    if blob[4:4 + n] != b'DSP56300':
        raise ValueError('not a gearmulator processor state')
    off = 4 + n + 4
    size = struct.unpack_from('<I', blob, off)[0]
    off += 4
    end = off + size
    while off + 12 <= end:
        cid = blob[off:off + 4]
        _, length = struct.unpack_from('<II', blob, off + 4)
        if cid == b'MIDI':
            data = blob[off + 12:off + 12 + length]
            plugin_len = struct.unpack_from('<I', data, 0)[0]
            plugin = data[4:4 + plugin_len]
            return plugin[2:]
        off += 12 + length
    raise ValueError('no MIDI chunk in the processor state')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    text = open(sys.argv[1], encoding='utf-8', errors='replace').read()
    match = re.search(r'<VALUE name="filterState" val="([^"]+)"', text)
    if not match:
        sys.exit('no filterState in the settings file')
    state = device_state(decode_memory_block(match.group(1)))
    open(sys.argv[2], 'wb').write(state)
    print(f'wrote {len(state)} bytes ({state[:4]!r})')


if __name__ == '__main__':
    main()
