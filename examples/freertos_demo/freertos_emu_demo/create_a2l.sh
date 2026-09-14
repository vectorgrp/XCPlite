#!/bin/bash

# A2L file creator for the freertos_emu_demo example project

# The script syncs the example project to the target, builds it there, runs it with XCP on Ethernet,
# downloads the ELF file to the local machine and creates an A2L file.  
# Prerequisites:
# - The target machine must be Linux
# - The target must be reachable via SSH and have rsync installed
# - The local machine must have rsync and scp installed
# - The local machine must have xcpclient installed


echo "========================================================================================================"
echo "A2L file creator for the freertos_emu_demo example project"
echo "========================================================================================================"


SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)" || exit 1
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)" || exit 1

if [ ! -f "$REPO_ROOT/build.sh" ] || [ ! -f "$REPO_ROOT/CMakeLists.txt" ]; then
    echo "❌ FAILED: Repository root not found at $REPO_ROOT" >&2
    exit 1
fi

#======================================================================================================================
# Parameters
#======================================================================================================================

# Build on the remote Linux target (true) or on the local machine (false)
# A local build is possible on Linux only: executables built on macOS (Mach-O) contain no DWARF debug information,
# the xcpclient A2L generator can not create an A2L file from them
REMOTE=true

# Run a short calibration and measurement test
TEST=false
CSVFILE="$REPO_ROOT/examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.csv"



LOGFILE="$REPO_ROOT/examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.log"
#LOGFILE='/dev/stdout'
#LOGFILE="/dev/null"

# A2L file path on local machine
A2LFILE="$REPO_ROOT/examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.a2l"

# ELF file path on local machine
ELFFILE="$REPO_ROOT/examples/freertos_demo/freertos_emu_demo/CANape/freertos_demo.elf"

# Build type for target executable: Release, RelWithDebInfo or Debug
# RelWithDebInfo is default to demonstrate operation with with -O1 and NDEBUG
# Optimization level >= -O1 keeps variables in registers whenever possible, so these local variables cannot be measured
# Debug mode is the least efficient but keeps all variables and stack frames intact
BUILD_TYPE="RelWithDebInfo"
# -O0
#BUILD_TYPE="Debug"
# -O2 no debug symbols
#BUILD_TYPE="Release"


# Target connection details
#TARGET_USER="parallels"
#TARGET_HOST="10.211.55.4"
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-rtos"
TARGET_BUILD_DIR="build-rtos"
TARGET_BINARY="freertos_emu_demo"

# Path to xcpclient tool executable (assuming cargo installed it to ~/.cargo/bin)
XCPCLIENT="xcpclient"


#======================================================================================================================
# Sync Target, Build Application on Target, Download ELF, Start ECU, ...
#======================================================================================================================

mkdir -p "$(dirname "$LOGFILE")"
echo "Logging to $LOGFILE enabled"
echo "" > "$LOGFILE"

#======================================================================================================================
# Remote build
# Sync target, build, upload ELF, start application on target
#======================================================================================================================

if [ "$REMOTE" = true ]; then

# Sync target
echo "Sync $REPO_ROOT/ to $TARGET_USER@$TARGET_HOST:$TARGET_PATH/ ..."
rsync -avz --delete \
    --include='/build.sh' \
    --include='/CMakeLists.txt' \
    --include='/cmake/***' \
    --include='/inc/***' \
    --include='/src/***' \
    --include='/examples/' \
    --include='/examples/freertos_demo/***' \
    --exclude='*' \
    "$REPO_ROOT/" "$TARGET_USER@$TARGET_HOST:$TARGET_PATH/" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi


# Build on target
# Always a clean build: if the target has no NTP and its clock may skew,
echo "Clean build executable on Target ..."
ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && ./build.sh $BUILD_TYPE rtos examples clean" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi


# Download the target executable for the local A2L generation process
echo "Downloading ELF file from target $TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY to $ELFFILE ..."
scp "$TARGET_USER@$TARGET_HOST:$TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY" "$ELFFILE" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Download $TARGET_PATH/$TARGET_BUILD_DIR/$TARGET_BINARY"
    exit 1
fi

else

# Build local
# Linux only: the macOS linker does not put the DWARF debug information into the executable (Mach-O),
# xcpclient can not create an A2L file from it
if [ "$(uname -s)" = "Darwin" ]; then
    echo "❌ FAILED: A local build on macOS creates a Mach-O executable without DWARF debug information, xcpclient can not create an A2L file from it"
    echo "   Build on a Linux target instead: set REMOTE=true in $SCRIPT_DIR/create_a2l.sh"
    exit 1
fi
echo "Build ..."
"$REPO_ROOT/build.sh" $BUILD_TYPE rtos examples 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build"
    exit 1
fi

cp "$REPO_ROOT/$TARGET_BUILD_DIR/$TARGET_BINARY" "$ELFFILE" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Copy $REPO_ROOT/$TARGET_BUILD_DIR/$TARGET_BINARY to $ELFFILE"
    exit 1
fi

fi


#======================================================================================================================
# Create A2L file
# Create the  A2L from ELF file with xcpclient tool
#======================================================================================================================

echo ""
echo "========================================================================================================"
echo "Creating A2L file from XCPlite ELF file ..."
echo "========================================================================================================"
echo ""
# --log-level is program flow verbosity
# --verbose is information detail level
# Remove the A2L file of a previous run, so a failed generation can not leave a stale A2L file behind
rm -f "$A2LFILE"
XCPCLIENT_ARGS=(--log-level=3 --verbose=5 --dest-addr="$TARGET_HOST" --udp --offline --elf "$ELFFILE" --elf-unit-filter xcp_demo --default-event=0 --create-a2l --a2l "$A2LFILE")
echo "Command: $XCPCLIENT ${XCPCLIENT_ARGS[*]}"
"$XCPCLIENT" "${XCPCLIENT_ARGS[@]}" >> "$LOGFILE"
if [ $? -ne 0 ] || [ ! -f "$A2LFILE" ]; then
    echo "❌ FAILED: xcpclient could not create the A2L file $A2LFILE, see $LOGFILE"
    grep "\[ERROR\]" "$LOGFILE"
    exit 1
fi



echo ""
echo "✅ SUCCESS:"
echo "Created a new A2L file $A2LFILE"
echo ""


#======================================================================================================================
# Test
#======================================================================================================================

if [ "$TEST" = true ]; then

ssh "$TARGET_USER@$TARGET_HOST" "cd $TARGET_PATH && ./$TARGET_BUILD_DIR/$TARGET_BINARY" &
sleep 1

echo "========================================================================================================"
echo "Test connect"
echo "========================================================================================================"
read -p "Press any key to continue..." -n1 -s
$XCPCLIENT --log-level=3 --dest-addr=$TARGET_HOST:5555 --udp --a2l "$A2LFILE" --list-mea .  --list-cal . 
sleep 1

echo "========================================================================================================"
echo "Test measurement"
echo "========================================================================================================"
read -p "Press any key to continue..." -n1 -s
$XCPCLIENT --log-level=3 --dest-addr=$TARGET_HOST:5555 --udp --a2l "$A2LFILE"  --mea counter --time 3 --csv "$CSVFILE"
sleep 1

ssh "$TARGET_USER@$TARGET_HOST" "pkill -f freertos_emu_demo" 

fi
