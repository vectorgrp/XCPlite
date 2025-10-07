# true: use the DanielT a2ltool to add measurements and characteristics to the A2L file template created by xcp_client
# false: use xcp_client with the elf file option to create the A2L file 
ENABLE_A2LTOOL=false
ONLINE=true

# Path to a2ltool executable
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Target A2L name to generate in local machines CANape project folder
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo.a2l"

# Path to ELF file in local machine CANape project folder
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"

# Path to xcp_client tool executable
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"


# Target connection details
TARGET_USER="rainer"
TARGET_HOST="192.168.0.206"
TARGET_PATH="~/XCPlite-RainerZ/build/no_a2l_demo.out"


# Sync target
echo "Sync target ..."            
rsync -avz --delete --exclude=build/ --exclude=.git/ --exclude="*.o" --exclude="*.out" --exclude="*.a" ./ rainer@192.168.0.206:~/XCPlite-RainerZ/ > /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Rsync with target"
    exit 1
fi


# Build on target
echo "Build executable on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && ./build.sh" > /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Build on target"
    exit 1
fi


# Download the target executable for local A2L generation
echo "Downloading executable from target $TARGET_PATH to $ELFFILE ..."
scp $TARGET_USER@$TARGET_HOST:$TARGET_PATH $ELFFILE > /dev/null
if [ $? -ne 0 ]; then
    echo "❌ FAILED: Download $TARGET_PATH"
    exit 1
fi



# Run target executable with XCP on Ethernet in background#
if [ $ONLINE == true ]; then
echo "Starting executable $TARGET_PATH on Target ..."
ssh $TARGET_USER@$TARGET_HOST "cd ~/XCPlite-RainerZ && $TARGET_PATH" &
SSH_PID=$!
sleep 2
fi


if [ $ENABLE_A2LTOOL == true ]; then
    
# Use a2ltool to update the A2L template with measurement 'counter' and characteristic 'params'
echo ""
echo "========================================================================================================"
echo "Creating A2L file $A2LFILE with xcp_client and a2ltool"
echo "========================================================================================================"

# Create a A2L template with xcp_client by uploading memory segments and events via XCP
$XCPCLIENT --log-level=2  --dest-addr=$TARGET_HOST:5555 --tcp  --create-a2l --a2l $A2LFILE.template.a2l 
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi
# Update A2L with a2ltool and add measurement 'counter' and characteristic 'params'
$A2LTOOL --verbose --update --measurement-regex "global_counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE $A2LFILE.template.a2l 
if [ $? -ne 0 ]; then
    echo "❌ FAILED: a2ltool returned error"
    exit 1
fi
cp tmp.a2l $A2LFILE

else

# Create a A2L template with xcp_client by uploading memory segments and events via XCP
echo ""
echo "========================================================================================================"
echo "Creating A2L file $A2LFILE with xcp_client"
echo "========================================================================================================"


if [ $ONLINE == true ]; then
$XCPCLIENT --log-level=3 --verbose --dest-addr=$TARGET_HOST:5555 --tcp  --elf $ELFFILE  --create-a2l --a2l $A2LFILE 
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi
else
$XCPCLIENT --log-level=2 --verbose --elf $ELFFILE   --a2l $A2LFILE 
if [ $? -ne 0 ]; then
    echo "❌ FAILED: xcp_client returned error"
    exit 1
fi
fi


fi


if [ $ONLINE == true ]; then

#echo "Start a test measurement"
#$XCPCLIENT --log-level=3  --dest-addr=$TARGET_HOST:5555 --tcp  --a2l $A2LFILE  --mea ".*" --time-ms 100


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

echo ""
echo "========================================================================================================"
echo "✅ SUCCESS:"
echo "Created a new A2L file $A2LFILE"
echo "========================================================================================================"

