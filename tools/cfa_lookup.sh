#!/bin/bash

# Simple CFA lookup script using objdump and nm
# Usage: ./cfa_lookup.sh <executable> <function_name>

if [ $# -lt 2 ]; then
    echo "Usage: $0 <executable> <function_name>"
    echo "       $0 <executable> --list-functions"
    exit 1
fi

EXECUTABLE="$1"
FUNCTION="$2"

if [ "$FUNCTION" = "--list-functions" ]; then
    echo "Available functions:"
    nm -n "$EXECUTABLE" | grep ' T ' | awk '{print $3 " (0x" $1 ")"}'
    exit 0
fi

# Get function address from symbol table
FUNC_ADDR=$(nm "$EXECUTABLE" | grep " T $FUNCTION$" | awk '{print $1}')

if [ -z "$FUNC_ADDR" ]; then
    echo "Function '$FUNCTION' not found"
    echo "Available functions:"
    nm -n "$EXECUTABLE" | grep ' T ' | head -10 | awk '{print "  " $3}'
    exit 1
fi

echo "Function: $FUNCTION"
echo "Address: 0x$FUNC_ADDR"

# Note: For actual CFA offset, we would need to parse .eh_frame section
# This requires more sophisticated tooling (like gimli in Rust)
echo "CFA Offset: [Requires .eh_frame parsing - use SSH to Pi with readelf for now]"
echo ""
echo "For manual lookup on Pi:"
echo "  ssh rainer@192.168.0.206 \"cd ~/XCPlite-RainerZ && readelf -wF ./build/no_a2l_demo.out | grep -A3 -B3 '$FUNC_ADDR'\""