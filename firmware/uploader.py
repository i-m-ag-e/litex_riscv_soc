#!/usr/bin/env python3
# =============================================================================
# uploader.py — PicoRV32 Firmware Uploader (Protocol v2)
# =============================================================================
#
# Protocol:
#   Phase 1 — Handshake:   TX SYNC(0x5A) → RX ACK(0x79) + FLASH_INFO(12 bytes)
#   Phase 2 — Setup:       TX SIZE(4B LE) + MODE(1B) → RX ACK/NAK
#   Phase 3 — Transfer:    TX DATA(size B) + CHECKSUM(4B LE) → RX ACK/NAK
#
# Flash info block (12 bytes, little-endian):
#   [0]     sfdp_valid  (0x01 = ok, 0x00 = failed)
#   [1]     erase_cmd
#   [2]     addr4 flag
#   [3]     reserved
#   [4..7]  sector_size
#   [8..11] page_size
#

import serial
import argparse
import struct
import sys
import os
import time

SYNC_BYTE  = b'\x5A'
ACK_BYTE   = b'\x79'
NAK_BYTE   = b'\x1F'

MODE_BURN  = 0x00
MODE_RAM   = 0x01

FLASH_INFO_SIZE = 16

def wait_for_ack(ser, phase_name, timeout=2.0):
    """Wait for ACK/NAK from bootloader.  Returns True on ACK."""
    start = time.time()
    while time.time() - start < timeout:
        b = ser.read(1)
        if b == ACK_BYTE:
            return True
        elif b == NAK_BYTE:
            print(f"  ✗ NAK received during: {phase_name}")
            return False
        elif b:
            print(f"  [noise: 0x{b.hex()}]")
    print(f"  ✗ Timeout ({timeout:.1f}s) during: {phase_name}")
    return False

def read_flash_info(ser, timeout=2.0):
    """Read and decode the 16-byte flash info block from the bootloader."""
    data = b''
    start = time.time()
    while len(data) < FLASH_INFO_SIZE and time.time() - start < timeout:
        chunk = ser.read(FLASH_INFO_SIZE - len(data))
        if chunk:
            data += chunk
    if len(data) < FLASH_INFO_SIZE:
        return None

    flash_src   = data[0]   # 0x01 = SFDP, 0x02 = hardcoded defaults
    erase_cmd   = data[1]
    addr4       = data[2]
    status_reg  = data[3]
    jedec_mfr   = data[4]
    jedec_type  = data[5]
    jedec_cap   = data[6]
    # data[7] is reserved
    sector_size = struct.unpack('<I', data[8:12])[0]
    page_size   = struct.unpack('<I', data[12:16])[0]

    return {
        'flash_src':   flash_src,
        'erase_cmd':   erase_cmd,
        'addr4':       bool(addr4),
        'status_reg':  status_reg,
        'jedec_id':    (jedec_mfr, jedec_type, jedec_cap),
        'sector_size': sector_size,
        'page_size':   page_size,
    }


def fmt_size(n):
    """Format a byte count with a human-readable suffix."""
    if n == 0:
        return "0"
    if n >= 1024 and n % 1024 == 0:
        return f"{n} ({n // 1024} KB)"
    return str(n)


def main():
    parser = argparse.ArgumentParser(
        description="PicoRV32 Firmware Uploader (Protocol v2)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  %(prog)s /dev/ttyUSB0 app.bin              # burn to flash + execute\n"
            "  %(prog)s /dev/ttyUSB0 app.bin --ram-only    # load to SRAM only\n"
            "  %(prog)s /dev/ttyUSB0 app.bin -b 230400     # custom baud rate\n"
        )
    )
    parser.add_argument("port", help="Serial port (e.g. /dev/ttyUSB0)")
    parser.add_argument("file", help="Compiled firmware .bin file")
    parser.add_argument("-b", "--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
    parser.add_argument("--ram-only", action="store_true",
                        help="Load into SRAM and execute without writing to flash. "
                             "Code will NOT survive a power cycle.")
    args = parser.parse_args()

    mode = MODE_RAM if args.ram_only else MODE_BURN
    mode_name = "RAM-only (no flash write)" if args.ram_only else "Burn to flash + Execute"

    # ── Load and validate firmware file ────────────────────────────────────
    if not os.path.exists(args.file):
        print(f"Error: File '{args.file}' not found.")
        sys.exit(1)

    with open(args.file, "rb") as f:
        firmware = f.read()

    file_size = len(firmware)
    if file_size == 0:
        print("Error: Firmware file is empty.")
        sys.exit(1)

    checksum = sum(firmware) & 0xFFFFFFFF

    # ── Header ─────────────────────────────────────────────────────────────
    print("─" * 52)
    print("  PicoRV32 Firmware Uploader  (Protocol v2)")
    print("─" * 52)
    print(f"  File:     {os.path.basename(args.file)}")
    print(f"  Size:     {file_size} bytes")
    print(f"  Checksum: 0x{checksum:08X}")
    print(f"  Mode:     {mode_name}")
    print(f"  Port:     {args.port} @ {args.baud} baud")
    print("─" * 52)
    print()

    if args.ram_only:
        print("  ⚠  RAM-only mode: code will NOT survive a power cycle.")
        print()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # ── Phase 1: Handshake ─────────────────────────────────────────────
        print("[1/4] Handshake")
        print("  → SYNC (0x5A)")
        ser.write(SYNC_BYTE)
        if not wait_for_ack(ser, "handshake"):
            print("  FAILED: Bootloader did not respond. Is the board in upload mode (SW0 HIGH)?")
            sys.exit(1)
        print("  ← ACK")

        # Read flash info block
        info = read_flash_info(ser)
        if info is None:
            print("  FAILED: Timed out reading flash info block.")
            sys.exit(1)

        # Display flash info
        print()
        print("[2/4] Flash Info")
        src = info['flash_src']
        if src == 0x01:
            src_label = "SFDP (JESD216 auto-detected)"
        elif src == 0x02:
            src_label = "Hardcoded defaults (SFDP not supported by chip)"
        else:
            src_label = f"Unknown (0x{src:02X})"
        print(f"  ├─ Source:         {src_label}")
        print(f"  ├─ JEDEC ID:       0x{info['jedec_id'][0]:02X} 0x{info['jedec_id'][1]:02X} 0x{info['jedec_id'][2]:02X}")
        print(f"  ├─ Status Reg:     0x{info['status_reg']:02X} (BP2={bool(info['status_reg']&0x10):d} BP1={bool(info['status_reg']&0x08):d} BP0={bool(info['status_reg']&0x04):d})")
        print(f"  ├─ Erase Opcode:   0x{info['erase_cmd']:02X}")
        print(f"  ├─ Sector Size:    {fmt_size(info['sector_size'])}")
        print(f"  ├─ Page Size:      {fmt_size(info['page_size'])}")
        print(f"  └─ 4-Byte Addr:    {'Yes (flash > 16 MB)' if info['addr4'] else 'No'}")

        if info['jedec_id'] == (0x00, 0x00, 0x00) or info['jedec_id'] == (0xFF, 0xFF, 0xFF):
            print("\n  [!] CRITICAL WARNING: SPI MISO line appears to be dead/stuck.")
            print("      Flash writes will silently fail. Check LiteX pin constraints!")
        elif (info['status_reg'] & 0x1C) != 0:
            print("\n  [!] WARNING: Flash chip has Block Protection bits set!")
            print("      Erase/Write commands will likely be ignored by the flash chip.")

        # ── Phase 2: Send size + mode ──────────────────────────────────────
        print()
        print("[3/4] Transfer Setup")
        print(f"  → Size: {file_size} bytes  |  Mode: 0x{mode:02X} ({'RAM' if args.ram_only else 'BURN'})")
        ser.write(struct.pack('<I', file_size))
        ser.write(bytes([mode]))
        if not wait_for_ack(ser, "transfer setup"):
            print("  FAILED: Bootloader rejected size or mode.")
            print("          Possible causes: payload too large for SRAM, or burn mode with no flash.")
            sys.exit(1)
        print("  ← ACK")

        # ── Phase 3: Stream data + checksum ────────────────────────────────
        print()
        print("[4/4] Data Transfer")
        start_time = time.time()

        ser.write(firmware)
        ser.write(struct.pack('<I', checksum))
        ser.flush()

        elapsed = time.time() - start_time
        rate = file_size / elapsed / 1024 if elapsed > 0 else 0
        print(f"  → Sent {file_size} bytes + checksum in {elapsed:.2f}s ({rate:.1f} KB/s)")

        print("  Waiting for validation...", end="", flush=True)

        # Burn can take several seconds for large payloads
        timeout = 10.0 if mode == MODE_BURN else 3.0
        if not wait_for_ack(ser, "final validation", timeout=timeout):
            print()
            print("  FAILED: Firmware validation failed or flash burn timed out.")
            sys.exit(1)

        print()
        print("  ← ACK")

        # ── Summary ───────────────────────────────────────────────────────
        print()
        print("─" * 52)
        if args.ram_only:
            print("  ✓ Loaded to SRAM and executing.")
            print("    (Will be lost on power cycle.)")
        else:
            print("  ✓ Burned to flash and executing.")
            print("    (Persists across power cycles.)")
        print("─" * 52)

    except serial.SerialException as e:
        print(f"\nSerial port error: {e}")
        sys.exit(1)
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()


if __name__ == "__main__":
    main()
