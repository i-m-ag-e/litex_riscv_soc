#!/usr/bin/env python3
import serial
import argparse
import struct
import sys
import os
import time

SYNC_BYTE = b'\x5A'
ACK_BYTE  = b'\x79'
NAK_BYTE  = b'\x1F'

def wait_for_ack(ser, timeout=2.0):
    start = time.time()
    while time.time() - start < timeout:
        b = ser.read(1)
        if b == ACK_BYTE:
            return True
        elif b == NAK_BYTE:
            print("\nError: FPGA reported a Checksum Verification Failure!")
            return False
        elif b:
            print(f"  [Ignoring line noise: {b.hex()}]")
    return False

def main():
    parser = argparse.ArgumentParser(description="PicoRV32 High-Speed Uploader")
    parser.add_argument("port", help="Serial port (e.g., /dev/ttyUSB0)")
    parser.add_argument("file", help="Compiled firmware .bin file")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"Error: File '{args.file}' not found.")
        sys.exit(1)

    with open(args.file, "rb") as f:
        firmware = f.read()
    
    file_size = len(firmware)
    # Calculate rolling 32-bit addition checksum
    checksum = sum(firmware) & 0xFFFFFFFF
    
    print(f"Loaded '{args.file}' ({file_size} bytes). Checksum: {hex(checksum)}")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.5)
        time.sleep(0.5) # Let connection settle
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
        # 1. Synchronization
        print("Sending SYNC byte...")
        ser.write(SYNC_BYTE)
        if not wait_for_ack(ser):
            print("Error: Expected ACK, got timeout")
            sys.exit(1)
            
        # 2. Send File Size
        print("Sync successful. Sending file size...")
        ser.write(struct.pack('<I', file_size))
        if not wait_for_ack(ser):
            print("Error: Failed to acknowledge size.")
            sys.exit(1)
            
        # 3. High-Speed Bulk Streaming
        print("Streaming payload continuously at maximum hardware bandwidth...")
        start_time = time.time()
        
        # Stream the entire binary block in a single transaction
        ser.write(firmware)
        
        # Immediately append the 4-byte validation checksum
        ser.write(struct.pack('<I', checksum))
        
        # Force Python to flush buffers down to hardware
        ser.flush()
        
        elapsed = time.time() - start_time
        print(f"Sent {file_size} bytes in {elapsed:.2f} seconds ({file_size/elapsed/1024:.2f} KB/s)")
        
        # 4. Final Verification Acknowledge
        print("Payload sent. Waiting for FPGA validation...")
        if not wait_for_ack(ser, timeout=5.0):
            print("Error: Firmware validation failed or timed out.")
            sys.exit(1)
            
        print("Transfer complete! Checksum verified. The FPGA is executing your code.")
        
    except serial.SerialException as e:
        print(f"Serial port error: {e}")
        sys.exit(1)
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()

if __name__ == "__main__":
    main()
