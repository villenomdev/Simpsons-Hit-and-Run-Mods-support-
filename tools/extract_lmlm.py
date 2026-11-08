#!/usr/bin/env python3
"""
extract_lmlm.py - Universal Lucas Mod Launcher (.lmlm) Extractor

Lucas Mod Launcher mods (.lmlm) can be distributed in two formats:
1. Standard ZIP format (PK\x03\x04): easily renamed to .zip.
2. Proprietary LSPA binary format (LSPA\x05\x00\x00\x00): uses a 512-byte aligned
   pre-order recursive directory tree and uncompressed sector-aligned file storage.

This tool automatically detects the format and extracts all files preserving
the exact directory structure (CustomFiles/, AdditionalFiles/, etc.).
"""

import os
import sys
import struct
import zipfile

def extract_zip(file_path, out_dir):
    print(f"[+] Detected Standard ZIP format in: {file_path}")
    with zipfile.ZipFile(file_path, 'r') as zf:
        zf.extractall(out_dir)
    print(f"[✓] Extracted ZIP contents to: {out_dir}")

def extract_lspa(file_path, out_dir):
    print(f"[+] Detected Lucas' Package Archive (LSPA) format in: {file_path}")
    with open(file_path, 'rb') as f:
        sig = f.read(4)
        if sig != b'LSPA':
            raise ValueError(f"Invalid magic signature: {sig} (expected b'LSPA')")
        
        # Scan timeline of directory headers and files
        # Header / directory metadata resides in 512-byte (0x200) blocks before data
        pos = 0x400
        timeline = []
        
        # Data offsets start at or after the directory table
        # We find the boundary dynamically by looking for valid file pointer offsets
        f.seek(0, os.SEEK_END)
        total_file_size = f.tell()
        f.seek(0x400)
        
        # Scan directory area (first 2MB max or until first data offset)
        min_data_offset = total_file_size
        
        while pos < min_data_offset:
            f.seek(pos)
            block = f.read(0x200)
            if len(block) < 0x200:
                break
                
            # Check for name block (UTF-16LE prefixed with \x02\x00)
            if block[0:2] == b'\x02\x00':
                name = block[2:].decode('utf-16le', errors='ignore').split('\x00')[0]
                
                # Next block contains metadata (SYSTEMTIME + size/offset or child count)
                f.seek(pos + 0x200)
                meta = f.read(0x200)
                if len(meta) >= 64:
                    ints = struct.unpack('<16I', meta[:64])
                    size = ints[3] | (ints[4] << 32)
                    offset = ints[5] | (ints[6] << 32)
                    
                    # Valid file data entry
                    if 0 <= size < total_file_size and 0 < offset < total_file_size:
                        timeline.append({'type': 'file', 'name': name, 'size': size, 'offset': offset, 'pos': pos})
                        if offset < min_data_offset:
                            min_data_offset = offset
                        pos += 0x400
                    else:
                        # Directory entry
                        count = ints[3] & 0xffff
                        flag = ints[3] >> 16
                        timeline.append({'type': 'dir', 'name': name, 'count': count, 'flag': flag, 'pos': pos})
            pos += 0x200

        print(f"[+] Found {len(timeline)} directory and file entries in LSPA header.")
        
        # Reconstruct directory tree using pre-order stack traversal
        root = {'name': '', 'children': []}
        stack = [[root, 999999]]
        extracted_files = 0
        total_bytes = 0
        
        for item in timeline:
            current_dir = '/'.join([s[0]['name'] for s in stack if s[0]['name']])
            
            if item['type'] == 'dir':
                dir_path = os.path.join(out_dir, current_dir, item['name'])
                os.makedirs(dir_path, exist_ok=True)
                node = {'name': item['name'], 'children': []}
                stack[-1][0]['children'].append(node)
                stack[-1][1] -= 1
                stack.append([node, item['count']])
            else:
                file_path = os.path.join(out_dir, current_dir, item['name'])
                os.makedirs(os.path.dirname(file_path), exist_ok=True)
                f.seek(item['offset'])
                data = f.read(item['size'])
                with open(file_path, 'wb') as out_f:
                    out_f.write(data)
                extracted_files += 1
                total_bytes += item['size']
                stack[-1][0]['children'].append(item)
                stack[-1][1] -= 1
                
            while len(stack) > 1 and stack[-1][1] == 0:
                stack.pop()

    print(f"[✓] Extracted {extracted_files} files ({total_bytes / (1024*1024):.2f} MB) to: {out_dir}")

def extract_lmlm(file_path, out_dir=None):
    if not os.path.isfile(file_path):
        print(f"Error: File not found: {file_path}")
        sys.exit(1)
        
    if out_dir is None:
        base_name = os.path.splitext(os.path.basename(file_path))[0]
        out_dir = os.path.join(os.path.dirname(file_path), base_name)
        
    os.makedirs(out_dir, exist_ok=True)
    
    with open(file_path, 'rb') as f:
        header = f.read(4)
        
    if header.startswith(b'PK\x03\x04') or header.startswith(b'PK'):
        extract_zip(file_path, out_dir)
    elif header == b'LSPA':
        extract_lspa(file_path, out_dir)
    else:
        # Check if it might be a nested zip or has an offset
        try:
            extract_zip(file_path, out_dir)
        except Exception:
            raise ValueError(f"Unknown mod package format with header: {header}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python3 extract_lmlm.py <mod.lmlm> [output_directory]")
        sys.exit(1)
        
    src_file = sys.argv[1]
    dest_dir = sys.argv[2] if len(sys.argv) > 2 else None
    extract_lmlm(src_file, dest_dir)
