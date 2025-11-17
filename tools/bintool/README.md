# bintool - XCPlite BIN File Tool

A command-line tool for XCPlite `.BIN` persistence files.  
Persistence features in XCPlite are enabled with OPTION_CAL_PERSISTENCE.  
For safety, it is essential to use the EPK version segment. This can be enabled with OPTION_CAL_SEGMENT_EPK.  

Disclaimer: This is an AI generated tool. Don't use it for production. Improper use may corrupt calibration data.  

## Overview

This tool provides comprehensive management of XCPlite binary persistence files:
- **BIN to HEX**: Extract calibration segment data to Intel-Hex format
- **HEX to BIN**: Update calibration segment data in a BIN file from Intel-Hex files
- **Dump/Inspect**: View file structure, events, segments, and hex dumps


## Usage

### Quick Start (Convenient Syntax)

**Convert BIN to HEX** (output filename auto-derived):
```bash
bintool input.bin
# Creates: input.hex
```

**Inspect BIN file contents**:
```bash
bintool input.bin --dump
bintool input.bin --dump --verbose  # With hex dump
```

**Apply HEX data back to BIN**:
```bash
bintool input.bin --apply-hex changes.hex
```


### Arguments

- `[BIN_FILE]` - Input .BIN file path (positional argument, or use `--bin`)
- `-b, --bin <FILE>` - Input .BIN file path (alternative to positional)
- `-o, --hex <FILE>` - Output .HEX file path (optional, auto-derived from input if omitted)
- `--apply-hex <FILE>` - Apply Intel-Hex file data to update the BIN file (HEX to BIN mode)
- `-d, --dump` - Dump BIN file header and segment information (does not convert)
- `-v, --verbose` - Enable verbose output (with `--dump`, shows hex data)
- `-h, --help` - Display help information
- `-V, --version` - Display version information

**Note**: Options `--hex`, `--apply-hex`, and `--dump` are mutually exclusive.



## Building

```bash
cd tools/bintool
cargo build --release
# Optional, install to ~/.cargo/bin/bintool or use the compiled binary will be in `target/release/bintool`
cargo install --path .  
```

### Examples


```bash

bintool cpp_demo.bin
  Converted 4 calibration segment(s) from 'cpp_demo.bin'
  Output written to 'cpp_demo.hex'


bintool --apply-hex cpp_demo.hex cpp_demo.bin
  Updated 4 of 4 segment(s) in 'cpp_demo.bin'


bintool cpp_demo.bin  --dump  --verbose


========================================
BIN File: cpp_demo.bin
========================================

HEADER:
  Signature:    XCPLITE__BINARY
  Version:      0x0204
  EPK:          21_41_01
  Event Count:  4
  CalSeg Count: 4

EVENTS:

Event 0:
  Name:         async
  ID:           0
  Index:        0
  Cycle Time:   1000000 ns
  Priority:     0

Event 1:
  Name:         mainloop
  ID:           1
  Index:        0
  Cycle Time:   0 ns
  Priority:     0

Event 2:
  Name:         SigGen1
  ID:           2
  Index:        0
  Cycle Time:   0 ns
  Priority:     0

Event 3:
  Name:         SigGen2
  ID:           3
  Index:        0
  Cycle Time:   0 ns
  Priority:     0

CALIBRATION SEGMENTS:

Segment 0:
  Name:    epk
  Index:   0
  Size:    8 bytes
  Address: 0x80000000

  Data (hex dump):
    80000000:  32 31 5F 34 31 5F 30 31                           |21_41_01|

Segment 1:
  Name:    kParameters
  Index:   1
  Size:    8 bytes
  Address: 0x80010000

  Data (hex dump):
    80010000:  E8 03 00 00 E8 03 00 00                           |........|

Segment 2:
  Name:    SigGen1
  Index:   2
  Size:    88 bytes
  Address: 0x80020000

  Data (hex dump):
    80020000:  00 00 00 00 00 00 29 40  00 00 00 00 00 00 00 00  |......)@........|
    80020010:  00 00 00 00 00 00 00 00  9A 99 99 99 99 99 D9 3F  |...............?|
    80020020:  00 00 00 00 00 00 00 3F  00 00 80 3F 00 00 00 3F  |.......?...?...?|
    80020030:  00 00 00 00 00 00 00 BF  00 00 80 BF 00 00 00 BF  |................|
    80020040:  00 00 00 00 00 00 00 00  00 00 00 00 E8 03 00 00  |................|
    80020050:  00 00 00 00 00 00 00 00                           |........|

Segment 3:
  Name:    SigGen2
  Index:   3
  Size:    88 bytes
  Address: 0x80030000

  Data (hex dump):
    80030000:  00 00 00 00 00 00 54 40  18 2D 44 54 FB 21 F9 3F  |......T@.-DT.!.?|
    80030010:  00 00 00 00 00 00 00 00  00 00 00 00 00 00 24 40  |..............$@|
    80030020:  00 00 00 00 CD CC CC 3D  9A 99 99 3E 9A 99 19 3F  |.......=...>...?|
    80030030:  CD CC 4C 3F 00 00 80 3F  CD CC 4C 3F 9A 99 19 3F  |..L?...?..L?...?|
    80030040:  9A 99 99 3E CD CC CC 3D  00 00 00 00 E8 03 00 00  |...>...=........|
    80030050:  00 00 00 00 00 00 00 00                           |........|


```



## Intel-Hex Output

The tool generates Intel-Hex files with:
- Extended Linear Address records for 32-bit addressing
- Data records (32 bytes per record)
- End-of-file record



## HEX to BIN Update Safety Features

When updating a BIN file from Intel-Hex data (`--apply-hex`), the tool enforces strict validation:

### 1. EPK Segment Protection
If the first segment is named "epk", its size and content must exactly match between BIN and HEX files. This prevents incompatible firmware updates.

### 2. Complete Segment Coverage
HEX data must completely cover each BIN segment. Partial segment updates are rejected to prevent calibration data corruption.
- HEX segment size must be ≥ BIN segment size
- If HEX has more data, only the required bytes are written

### 3. Atomic Updates
All validations occur **before** any file modifications:
1. **Phase 1**: Read all segment descriptors
2. **Phase 2**: Validate all segments completely
3. **Phase 3**: Only if all validations pass, write the data

This ensures the original BIN file remains completely unchanged if any validation fails.

## Dependencies

- [clap](https://crates.io/crates/clap) - Command-line argument parsing
- [ihex](https://crates.io/crates/ihex) - Intel-Hex file format support
- [thiserror](https://crates.io/crates/thiserror) - Error handling





## License

MIT License - See LICENSE file in the project root.

## Author

RainerZ

