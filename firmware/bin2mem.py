#!/usr/bin/env python3

import argparse
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Convert a binary file to a 32-bit word .mem file for $readmemh."
    )
    parser.add_argument("input", help="Input binary (.bin) file")
    args = parser.parse_args()

    input_path = Path(args.input)

    if not input_path.is_file():
        print(f"Error: '{input_path}' is not a file.", file=sys.stderr)
        sys.exit(1)

    output_path = input_path.with_suffix(".mem")

    with input_path.open("rb") as fin, output_path.open("w") as fout:
        while True:
            word = fin.read(4)
            if not word:
                break

            # Pad the last partial word with zeros.
            if len(word) < 4:
                word += b"\x00" * (4 - len(word))

            value = int.from_bytes(word, byteorder="little")
            fout.write(f"{value:08x}\n")

    print(f"Created: {output_path}")


if __name__ == "__main__":
    main()
