#!/usr/bin/env python3
"""
generate_oversized.py — Create a firmware.bin that is too large for the bootloader.

Usage:
    python3 tests/generate_oversized.py [size_bytes]

    size_bytes  Target binary size (default: 34816 = 34 KB).
                Must be > 32764 (MAIN_RAM_SIZE - 4) to trigger the bootloader's
                size guard.

Output:
    firmware.bin  in the current directory, filled with a recognisable pattern.

What the bootloader does with this:
    State C receives the 4-byte size field, checks:
        if (size == 0 || size > (MAIN_RAM_SIZE - 4))   → NAK + halt
    The NAK fires BEFORE any payload data is sent, so SRAM is untouched.

Expected uploader output:
    [3/4] Transfer Setup
      ✗ NAK received during: transfer setup
      FAILED: Bootloader rejected size or mode.

Expected board LEDs:
    0xF0 (upper nibble on) — error/halt state.
    Press BTNC to recover.
"""

import sys
import os
import struct

MAIN_RAM_SIZE = 0x8000          # 32 KB — must match your LiteX build
MAX_VALID     = MAIN_RAM_SIZE - 4  # 32764 bytes

DEFAULT_SIZE  = 34 * 1024       # 34 KB → clearly oversized

def main():
    target = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SIZE

    if target <= MAX_VALID:
        print(f"WARNING: {target} bytes is within the valid range (<= {MAX_VALID}).")
        print("         The bootloader will ACCEPT this. Use a larger size.")
        sys.exit(1)

    # Fill with a repeating pattern that makes it obvious this is test data
    # (not accidental zeros).  Pattern: 0xDE 0xAD 0xC0 0xDE repeated.
    pattern = b'\xDE\xAD\xC0\xDE'
    data = (pattern * ((target // 4) + 1))[:target]

    out = "firmware.bin"
    with open(out, "wb") as f:
        f.write(data)

    print(f"Generated {out}: {target} bytes ({target/1024:.1f} KB)")
    print(f"Max valid size for this SoC: {MAX_VALID} bytes ({MAX_VALID/1024:.1f} KB)")
    print(f"This binary exceeds the limit by {target - MAX_VALID} bytes.")
    print()
    print("Now run:")
    print(f"  python3 uploader.py /dev/ttyUSB0 {out}")
    print("Expected: NAK during transfer setup, board LEDs = 0xF0")

if __name__ == "__main__":
    main()
