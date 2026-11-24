#!/usr/bin/env python3
"""Convert bootloader.bin into rom.v (combinational case-statement ROM).

SCL180 has no ROM macro, so the bootloader is baked into a LUT.
Usage: python3 scripts/bin2rom_v.py firmware/bootloader.bin rtl/rom.v
"""
import struct
import sys

DEPTH = 1024  # 1024 x 32-bit words (4 KB ROM), 10-bit address


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    data += b"\x00" * (-len(data) % 4)  # pad last word
    words = [struct.unpack("<I", data[i:i + 4])[0] for i in range(0, len(data), 4)]
    if len(words) > DEPTH:
        sys.exit(f"bootloader is {len(words)} words, ROM holds {DEPTH}")

    out = [
        "`timescale 1ns/1ps\n\n\n",
        "module rom(\n\tinput clk,\n\tinput [9:0] addr,\n\toutput reg [31:0] o\n);\n\n",
        "reg [31:0] data_out;\n\n",
        "always @(posedge clk) o <= data_out;\n\n",
        "// combinational Look up table:\n",
        "always @(*) begin\n\tcase (addr)\n",
    ]
    for a, w in enumerate(words):
        out.append(f"\t    10'h{a:03x}: data_out = 32'h{w:08x};\n")
    out.append("\t    default: data_out = 32'h00000013;\n")  # NOP
    out.append("\tendcase\nend\nendmodule\n")
    open(sys.argv[2], "w").write("".join(out))
    print(f"{sys.argv[2]}: {len(words)} words")


if __name__ == "__main__":
    main()
