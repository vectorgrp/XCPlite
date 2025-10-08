# Gimli CFA Lookup Demo

This Rust application demonstrates how to use the [gimli](https://github.com/gimli-rs/gimli) library to extract CFA (Canonical Frame Address) offset information from ELF/DWARF files. This is particularly useful for A2L file generators that need to determine the correct addresses of local variables in stack frames.

## What is CFA?

The **Canonical Frame Address (CFA)** is a DWARF debugging concept that represents the address of the call frame. For local variables stored on the stack, their addresses are typically calculated as:

```
local_variable_address = CFA + variable_offset
```

The CFA offset is function-specific and tells us how to calculate the frame base from the stack pointer.

## Features

- 🔍 Extract CFA offset information for functions in ELF/DWARF files
- 📋 List all functions with their debug information
- 🎯 Look up specific functions by name
- 📊 Show compilation unit information
- 💡 Provide guidance for A2L file generation

## Building

From the `tools/gimli_demo` directory:

```bash
cargo build --release
```

## Usage

### Basic usage - analyze the no_a2l_demo binary

```bash
cargo run -- ../../examples/no_a2l_demo/CANape/no_a2l_demo.out
```

### Look up a specific function

```bash
cargo run -- -f main ../../examples/no_a2l_demo/CANape/no_a2l_demo.out
cargo run -- -f foo ../../examples/no_a2l_demo/CANape/no_a2l_demo.out  
cargo run -- -f task ../../examples/no_a2l_demo/CANape/no_a2l_demo.out
```

### List all functions

```bash
cargo run -- --list-all ../../examples/no_a2l_demo/CANape/no_a2l_demo.out
```

### Verbose output with DWARF details

```bash
cargo run -- --verbose ../../examples/no_a2l_demo/CANape/no_a2l_demo.out
```

## Understanding the Output

The tool provides information about:

1. **Function Location**: Address range and compilation unit
2. **CFA Offset**: The offset from stack pointer to calculate frame base
3. **A2L Integration Guidance**: How to use this information in A2L generators

### Example Output

```
✅ Function 'main' found:

  📍 Location:
    Compilation Unit: 0
    Address Range: 0x00001234 - 0x00001456
    Size: 546 bytes

  🎯 CFA Information:
    CFA Offset: 16 (0x10)
    ✨ For A2L generation:
       Local variable address = CFA + 16 + variable_stack_offset
       Where CFA is the Canonical Frame Address at function entry

  💡 Usage for A2L Generation:
     1. Use this CFA offset as the base for local variable addresses
     2. Add the variable's stack offset (from DWARF location expressions)
     3. The result is the offset from the current stack pointer
```

## Integration with A2L Generators

To integrate this functionality into your A2L generator:

1. **Parse DWARF information** using gimli to get function CFA offsets
2. **For each local variable**:
   - Extract its DWARF location expression
   - Parse the location to get the stack offset relative to frame base
   - Calculate final address: `CFA + cfa_offset + variable_offset`
3. **Generate A2L entries** with the correct addresses

## Key DWARF Concepts

- **DIE (Debug Information Entry)**: Represents program entities (functions, variables, types)
- **DW_TAG_subprogram**: DWARF tag for functions
- **DW_AT_frame_base**: Attribute describing how to calculate frame base
- **DW_AT_low_pc/high_pc**: Function address range
- **DWARF Expressions**: Stack-machine programs for calculating addresses

## Limitations

This is a demonstration tool with some limitations:

- **Simple CFA parsing**: Only handles basic DWARF expressions
- **Static analysis**: Doesn't handle runtime frame layout changes
- **Architecture specific**: Assumes little-endian, may need adjustments for other architectures

For production A2L generators, you may need more sophisticated DWARF expression evaluation.

## Dependencies

- **gimli**: DWARF parsing library
- **object**: ELF file parsing
- **memmap2**: Memory mapping for efficient file access
- **clap**: Command line argument parsing
- **anyhow**: Error handling

## Example Functions in no_a2l_demo

The `no_a2l_demo.out` binary contains these functions of interest:

- **`main`**: Main function with local variables `counter`, `static_counter`, `delay_us`
- **`foo`**: Function with multiple local test variables of different types
- **`task`**: Thread function with `thread_local_counter` and `counter`

Each function has different local variables that your A2L generator needs to address correctly using their respective CFA offsets.
