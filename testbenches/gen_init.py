#!/usr/bin/env python3
"""Generate the main_ram $readmemh init files for the two SoC boot testbenches.

The DUT (asic_top_fixed.v) hardcodes `$readmemh("top_main_ram.init", main_ram)`,
so the shared file must hold different content depending on which test runs:

  warmboot (Test 1, tb_gpio_blinky, prog_mode=0):
      top_main_ram.init = firmware words + zero-fill + WARM_BOOT_MAGIC at the last
      word, so the bootloader takes State A and jumps to the resident firmware.

  upload (Test 2, tb_uart_upload, prog_mode=1):
      top_main_ram.init = all zeros (a non-runnable stub — jumping to it hits an
      all-zero illegal instruction and traps, so it can never emit 0xAA).
      test_gpio_payload.init = firmware words only, streamed over UART by the bench.
      This way portb_o==0xAA can only appear if the UART store path actually landed
      the firmware in RAM — it is not masked by a pre-resident copy.

      The bring-up tests (tb_spi_loopback, tb_uart1_loopback, tb_interrupt) also use
      upload mode, but pass --payload <name>. That selects a SELF-DESCRIBING payload:
      the first word is the exact firmware byte count, followed by the firmware words.
      The bench reads word 0 as the size, so the testbench never hardcodes a length.
      These tests cannot pre-load main_ram at all (the SPRAM macros have no $readmemh),
      so uploading over UART is the only way to get firmware in.

Word packing matches firmware/bin2mem.py exactly: little-endian 32-bit words,
last partial word zero-padded.
"""

import argparse
from pathlib import Path
import sys

RAM_WORDS = 8192          # main_ram depth (asic_top_fixed.v main_ram[0:8191])
WARM_BOOT_MAGIC = 0xDEADBEEF


def bin_to_words(path: Path):
    """Read a binary file into a list of little-endian 32-bit words (int)."""
    data = path.read_bytes()
    words = []
    for i in range(0, len(data), 4):
        chunk = data[i:i + 4]
        if len(chunk) < 4:
            chunk = chunk + b"\x00" * (4 - len(chunk))
        words.append(int.from_bytes(chunk, "little"))
    return words


def write_init(path: Path, words):
    """Write a list of 32-bit ints as one 8-hex-digit word per line."""
    with path.open("w") as f:
        for w in words:
            f.write(f"{w & 0xFFFFFFFF:08x}\n")
    print(f"Wrote {path} ({len(words)} words)")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mode", required=True, choices=["warmboot", "upload"])
    ap.add_argument("--fw", default="firmware/tests/test_gpio.bin",
                    help="firmware binary (relative to project root)")
    ap.add_argument("--root", default="..",
                    help="project root where the .init files are read by the sim")
    ap.add_argument("--payload", default=None,
                    help="upload mode: payload output filename. When given, the file is "
                         "written in self-describing form (word 0 = firmware byte count, "
                         "then firmware words) for the bring-up testbenches. When omitted, "
                         "the legacy pure-firmware test_gpio_payload.init is written for "
                         "tb_uart_upload.v.")
    args = ap.parse_args()

    root = Path(args.root)
    fw_path = root / args.fw
    if not fw_path.is_file():
        print(f"Error: firmware '{fw_path}' not found", file=sys.stderr)
        sys.exit(1)

    fw = bin_to_words(fw_path)
    if len(fw) > RAM_WORDS - 1:
        print(f"Error: firmware {len(fw)} words exceeds RAM capacity", file=sys.stderr)
        sys.exit(1)

    if args.mode == "warmboot":
        # firmware + zero-fill + magic at the final word
        ram = fw + [0] * (RAM_WORDS - len(fw))
        ram[RAM_WORDS - 1] = WARM_BOOT_MAGIC
        write_init(root / "top_main_ram.init", ram)
    else:  # upload
        # non-runnable stub in RAM; firmware travels over UART instead
        write_init(root / "top_main_ram.init", [0] * RAM_WORDS)
        if args.payload is None:
            # legacy format for tb_uart_upload.v: pure firmware words (size hardcoded in TB)
            write_init(root / "test_gpio_payload.init", fw)
        else:
            # self-describing format for the bring-up TBs: header word = exact byte count
            nbytes = fw_path.stat().st_size
            write_init(root / args.payload, [nbytes] + fw)


if __name__ == "__main__":
    main()
