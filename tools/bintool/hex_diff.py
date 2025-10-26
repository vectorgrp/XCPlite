#!/usr/bin/env python3
"""
Detailed comparison of Intel HEX files - reconstructs segments and compares byte-by-byte.
"""

import sys
from collections import defaultdict

def parse_hex_file(filename):
    """Parse Intel HEX file and return dict of address -> data"""
    memory = {}
    extended_addr = 0
    
    with open(filename, 'r') as f:
        for line_num, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            
            if not line.startswith(':'):
                print(f"Warning: Line {line_num} doesn't start with ':'")
                continue
            
            # Parse record
            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)
            
            if record_type == 0x00:  # Data record
                full_addr = extended_addr | address
                data_bytes = bytes.fromhex(line[9:9+byte_count*2])
                # Store each byte individually to handle overlapping/fragmented records
                for i, byte in enumerate(data_bytes):
                    memory[full_addr + i] = byte
                
            elif record_type == 0x01:  # End of file
                break
                
            elif record_type == 0x02:  # Extended Segment Address
                seg = int(line[9:13], 16)
                extended_addr = seg << 4
                
            elif record_type == 0x04:  # Extended Linear Address
                ela = int(line[9:13], 16)
                extended_addr = ela << 16
                
            elif record_type == 0x05:  # Start Linear Address
                pass  # Ignore
                
    return memory

def reconstruct_segments(memory):
    """Reconstruct contiguous segments from memory map"""
    if not memory:
        return []
    
    segments = []
    sorted_addrs = sorted(memory.keys())
    
    start_addr = sorted_addrs[0]
    data = bytearray([memory[start_addr]])
    
    for i in range(1, len(sorted_addrs)):
        addr = sorted_addrs[i]
        if addr == sorted_addrs[i-1] + 1:
            # Contiguous
            data.append(memory[addr])
        else:
            # Gap found, save current segment and start new one
            segments.append((start_addr, bytes(data)))
            start_addr = addr
            data = bytearray([memory[addr]])
    
    # Don't forget the last segment
    segments.append((start_addr, bytes(data)))
    
    return segments

def compare_segments(file1, file2):
    """Compare two hex files segment by segment"""
    print(f"Parsing {file1}...")
    memory1 = parse_hex_file(file1)
    segments1 = reconstruct_segments(memory1)
    
    print(f"Parsing {file2}...")
    memory2 = parse_hex_file(file2)
    segments2 = reconstruct_segments(memory2)
    
    print(f"\n{'='*80}")
    print(f"Segment Analysis:")
    print(f"{'='*80}\n")
    
    print(f"File 1: {file1}")
    total_bytes1 = sum(len(data) for _, data in segments1)
    print(f"  Segments: {len(segments1)}")
    print(f"  Total bytes: {total_bytes1}")
    for addr, data in segments1:
        print(f"    0x{addr:08X}: {len(data):4d} bytes")
    
    print(f"\nFile 2: {file2}")
    total_bytes2 = sum(len(data) for _, data in segments2)
    print(f"  Segments: {len(segments2)}")
    print(f"  Total bytes: {total_bytes2}")
    for addr, data in segments2:
        print(f"    0x{addr:08X}: {len(data):4d} bytes")
    
    # Create maps for easier comparison
    seg_map1 = {addr: data for addr, data in segments1}
    seg_map2 = {addr: data for addr, data in segments2}
    
    all_addrs = sorted(set(seg_map1.keys()) | set(seg_map2.keys()))
    
    print(f"\n{'='*80}")
    print(f"Detailed Comparison:")
    print(f"{'='*80}\n")
    
    identical = True
    
    for addr in all_addrs:
        in1 = addr in seg_map1
        in2 = addr in seg_map2
        
        if in1 and in2:
            data1 = seg_map1[addr]
            data2 = seg_map2[addr]
            
            if data1 == data2:
                print(f"✓ Segment at 0x{addr:08X}: IDENTICAL ({len(data1)} bytes)")
            else:
                print(f"✗ Segment at 0x{addr:08X}: DIFFERENT")
                print(f"  File1: {len(data1)} bytes")
                print(f"  File2: {len(data2)} bytes")
                
                # Show first difference
                min_len = min(len(data1), len(data2))
                for i in range(min_len):
                    if data1[i] != data2[i]:
                        print(f"  First difference at offset {i} (0x{addr+i:08X}):")
                        start = max(0, i-4)
                        end = min(min_len, i+12)
                        print(f"    File1[{start}:{end}]: {data1[start:end].hex()}")
                        print(f"    File2[{start}:{end}]: {data2[start:end].hex()}")
                        break
                
                if len(data1) != len(data2):
                    print(f"  Length mismatch: File1={len(data1)}, File2={len(data2)}")
                
                identical = False
        
        elif in1:
            print(f"✗ Segment at 0x{addr:08X}: Only in {file1} ({len(seg_map1[addr])} bytes)")
            # Show content if small
            if len(seg_map1[addr]) <= 32:
                print(f"  Data: {seg_map1[addr].hex()}")
                # Try to decode as ASCII
                try:
                    ascii_str = seg_map1[addr].decode('ascii')
                    if all(32 <= ord(c) < 127 or c == '\n' for c in ascii_str):
                        print(f"  ASCII: {ascii_str}")
                except:
                    pass
            identical = False
        
        else:
            print(f"✗ Segment at 0x{addr:08X}: Only in {file2} ({len(seg_map2[addr])} bytes)")
            if len(seg_map2[addr]) <= 32:
                print(f"  Data: {seg_map2[addr].hex()}")
            identical = False
    
    print(f"\n{'='*80}")
    if identical:
        print(f"✓✓✓ FILES ARE IDENTICAL! ✓✓✓")
    else:
        print(f"✗✗✗ FILES DIFFER ✗✗✗")
    print(f"{'='*80}\n")
    
    return identical

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: compare_hex_detailed.py <file1.hex> <file2.hex>")
        sys.exit(1)
    
    result = compare_segments(sys.argv[1], sys.argv[2])
    sys.exit(0 if result else 1)
