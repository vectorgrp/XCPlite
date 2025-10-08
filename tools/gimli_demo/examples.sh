#!/bin/bash

# Example script for using the CFA lookup tool
# This demonstrates common usage patterns

echo "=== CFA Lookup Examples ==="
echo

# Get the binary path (relative to the script location)
BINARY_PATH="../../examples/no_a2l_demo/CANape/no_a2l_demo.out"

echo "🔧 Building the CFA lookup tool..."
cargo build --release
echo

echo "📊 1. Basic analysis - show main, foo, and task functions:"
cargo run --release -- "$BINARY_PATH"
echo

echo "🔍 2. Look up a specific function (foo):"
cargo run --release -- -f foo "$BINARY_PATH"
echo

echo "📝 3. Show first 10 functions with details:"
cargo run --release -- --list-all "$BINARY_PATH" | head -30
echo

echo "🔬 4. Verbose analysis of main function:"
cargo run --release -- --verbose -f main "$BINARY_PATH"
echo

echo "✨ Done! Try these commands yourself:"
echo "  cargo run -- --help                           # Show all options"
echo "  cargo run -- -f <function_name> <elf_file>    # Look up specific function"
echo "  cargo run -- --list-all <elf_file>            # List all functions"
echo "  cargo run -- --verbose <elf_file>             # Show detailed DWARF info"