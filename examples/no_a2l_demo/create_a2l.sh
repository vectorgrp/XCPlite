# !/bin/bash

# A2L file creator for the no_a2l_demo example project

# The script syncs the example project to the target, builds it there, runs it with XCP on Ethernet,
# downloads the ELF file to the local machine and creates an A2L file either with xcp_client or with a2ltool
# Prerequisites:
# - The target must be reachable via SSH and have rsync installed
# - The local machine must have rsync and scp installed
# - The local machine must have xcp_client and a2ltool compiled and available


#======================================================================================================================
# Parameters
#======================================================================================================================

#LOGFILE="examples/no_a2l_demo/CANape/no_a2l_demo.log"
LOGFILE='/dev/stdout'
#LOGFILE="/dev/null"

# Target A2L name to generate
# The A2L file is generated in the local machine CANape project directory (examples/no_a2l_demo/CANape/)
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo.a2l"
A2LFILE_TEMPLATE="examples/no_a2l_demo/CANape/no_a2l_demo.template.a2l"

# Path to ELF file in local machine CANape project folder
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"

# Build type for target executable: Release, RelWithDebInfo or Debug
# RelWithDebInfo is default to demonstrate operation with with -O1 and NDEBUG
BUILD_TYPE="RelWithDebInfo"
# -O0
#BUILD_TYPE="Debug"
# -O2 no debug symbols
#BUILD_TYPE="Release"

# Release build will of course not work because debug symbols are removed


# Optimization level >= -O1 keeps variables in registers whenever possible, so local variables cannot be measured in any case
# The most efficient solution to keep local variables measurable is to use the DaqCapture macro, another option is mark the variable with volatile ot with the provided macro XCP_MEA
# Debug mode is the least efficient but keeps all variables and stack frames intact
# So far, the solution works well with -O1


# true: use the DanielT a2ltool to add measurements and characteristics to the A2L file template created by xcp_client
# false: use xcp_client with the elf file option to create the A2L file 
ENABLE_A2LTOOL=false
#ENABLE_A2LTOOL=true

# Connect to the target system possible
#ECU_ONLINE=true
ECU_ONLINE=true

# Connect to the target XCP server possible
# Start the XCP server on the target in background and stop it after A2L creation
#XCP_ONLINE=true
XCP_ONLINE=true

# Use the offline A2L creator and then update the A2L with the online A2L updater when the ECU is online
#OFFLINE_A2L_CREATION_AND_FIX=false
OFFLINE_A2L_CREATION_AND_FIX=true

# Target connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ/build/no_a2l_demo.out"

# Path to a2ltool executable
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to xcp_client tool executable
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"


# Run a simple measurement with the created A2L file to verify that measurements and characteristics are working
#TEST=true
TEST=false

#======================================================================================================================
# Sync Target, Build Application on Target, Download ELF, Start ECU, ...
#======================================================================================================================


echo "========================================================================================================"
echo "A2L file creator for the no_a2l_demo example project"
echo "========================================================================================================"

echo "Logging to $LOGFILE enabled"
echo "" > $LOGFILE


if [ $ECU_ONLINE == true ]; then

# Sync target
echo "Sync target ..."            
rsync -avz --delete --exclude=build/ --exclude=.git/ --exclude="*.o" --exclude="*.out" --exclude="*.a" ./ rainer@192.168.0.206:~/XCPlite-RainerZ/ 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi


# Build on target
echo "Build executable on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && ./build.sh $BUILD_TYPE" 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi


# Download the target executable for the local A2L generation process
echo "Downloading executable from target $TARGET_PATH to $ELFFILE ..."
scp $TARGET_USER@$TARGET_HOST:$TARGET_PATH $ELFFILE 1> /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Download $TARGET_PATH"
    exit 1
fi


# Run target executable with XCP on Ethernet in background#
if [ $XCP_ONLINE == true ]; then
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo.out" 1> /dev/null
echo ""
echo "Starting executable $TARGET_PATH on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
SSH_PID=$!
sleep 2
fi

fi

#======================================================================================================================
# Create A2L file
#======================================================================================================================

#----------------------------------------------------------------------------------------------------------------------
# a2ltool
if [ $ENABLE_A2LTOOL == true ]; then
    
# Use a2ltool to update the A2L template with measurement 'counter' and characteristic 'params'
echo ""
echo "Creating A2L file $A2LFILE with xcp_client and a2ltool in combination"

# Create a A2L template with xcp_client by uploading memory segments and events via XCP
$XCPCLIENT --log-level=2  --dest-addr=$TARGET_HOST --tcp  --create-a2l --a2l $A2LFILE.template.a2l  >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi
# Update A2L with a2ltool and add measurement 'counter' and characteristic 'params'
$A2LTOOL --verbose --update --measurement-regex "global_counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE $A2LFILE.template.a2l >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: a2ltool returned error"
    exit 1
fi
cp tmp.a2l $A2LFILE

#----------------------------------------------------------------------------------------------------------------------
else

#----------------------------------------------------
# Create a A2L template offline with xcp_client by uploading memory segments and events via XCP

if [ $OFFLINE_A2L_CREATION_AND_FIX == false ]; then
echo ""
echo "========================================================================================================"
echo "Creating A2L file in online mode via XCP event and segment information and from XCPlite ELF file ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3  --verbose=0 --dest-addr=$TARGET_HOST --tcp --elf $ELFFILE  --a2l $A2LFILE --create-a2l >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi

#---------------------------------------------------
else
echo ""
echo "========================================================================================================"
echo "Creating A2L file from XCPlite ELF file ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --tcp --offline --elf $ELFFILE  --create-a2l --a2l $A2LFILE_TEMPLATE  >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi

if [ $XCP_ONLINE == true ]; then

cp $A2LFILE_TEMPLATE $A2LFILE
echo ""
echo "========================================================================================================"
echo "Fixing A2L file with XCP server information to correct event ids, segment numbers and IF_DATA IP address ..."
echo "========================================================================================================"
echo ""
$XCPCLIENT --log-level=3 --verbose=0 --dest-addr=$TARGET_HOST --tcp --elf $ELFFILE   --a2l $A2LFILE  --fix-a2l >> $LOGFILE
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi
fi


fi


fi


#======================================================================================================================
# Shutdown
#======================================================================================================================


echo ""
echo ""
echo ""


if [ $ECU_ONLINE == true ]; then
if [ $XCP_ONLINE == true ]; then

# Stop target - kill SSH process if it still exists, then kill remote process by name
echo ""
echo "Stopping target executable $SSH_PID on Target ..."
if kill -0 $SSH_PID 2>/dev/null; then
    kill $SSH_PID
fi
# Also kill the remote process by name to be sure
# ssh rainer@192.168.0.206  "pkill -f no_a2l_demo.out" 
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo.out" 2>/dev/null || true
# Give target some time to shutdown
sleep 1

fi
fi

echo ""
echo "✅ SUCCESS:"
echo "Created a new A2L file $A2LFILE"
echo ""


#======================================================================================================================
# Test
#======================================================================================================================

if [ $TEST == true ]; then


echo "========================================================================================================"
echo "Test measurement"
echo "========================================================================================================"

echo "Start a test measurement"
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
sleep 1
$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp  --a2l $A2LFILE  --mea ".*" --time-ms 100
ssh $TARGET_USER@$TARGET_HOST "pkill -f no_a2l_demo.out" 


fi