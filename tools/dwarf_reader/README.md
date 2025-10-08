# DWARF CFA Reader

A command-line tool to look up CFA (Canonical Frame Address) offsets for functions in ELF executables.

## Build

```bash
cd tools/dwarf_reader
cargo build --release
```

## Usage

### Look up CFA offset for a specific function

```bash
./target/release/dwarf_reader -e ../../build/no_a2l_demo.out -f main
```

### Look up function in specific compilation unit

```bash
./target/release/dwarf_reader -e ../../build/no_a2l_demo.out -f main -c no_a2l_demo.c
```

### List all available functions

```bash
./target/release/dwarf_reader -e ../../build/no_a2l_demo.out --list-functions
```

## Example Output

```
Function: main
Compilation Unit: no_a2l_demo.c
PC Range: 0x2054-0x2460
CFA Offset: 96 bytes

For A2L generation:
  Variable address = CFA offset (96) + DWARF variable offset
  Example: If DWARF says DW_OP_fbreg -24, then A2L address = 96 + (-24) = 72
```

## Integration with A2L Generation

Use this tool in your A2L generation workflow:

```bash
# Get CFA offset for a function
CFA_OFFSET=$(./dwarf_reader -e executable.out -f function_name | grep "CFA Offset:" | cut -d' ' -f3)

# Calculate A2L addresses: CFA_OFFSET + DWARF_variable_offset
A2L_ADDRESS=$((CFA_OFFSET + DWARF_OFFSET))
```

## Command Line Options

- `-e, --executable <FILE>`: Path to ELF executable (required)
- `-f, --function <NAME>`: Function name to look up (required)
- `-c, --compilation-unit <FILE>`: Compilation unit filter (optional)
- `-l, --list-functions`: List all available functions
- `-h, --help`: Show help message
