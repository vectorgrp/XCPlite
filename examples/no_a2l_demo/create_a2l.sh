# Target A2L
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo_created.a2l"


# Path to a2ltool
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to ELF file
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"

# Path to xcp_client
XCPCLIENT="../xcp-lite-RainerZ/target/debug/xcp_client"

# Target
TARGET="build/no_a2l_demo.out"


# @@@@ Does not work: creates TYPEDEF_MEASUREMENT instead of TYPEDEF_CHARACTERISTIC for params
# Create new A2L file with selected measurements and characteristics
# $A2LTOOL --verbose --create --measurement-regex "counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE

# @@@@ Does not work: creates TYPEDEF_MEASUREMENT instead of TYPEDEF_CHARACTERISTIC for params
# Update the A2L template with measurement counter and characteristic params
# $A2LTOOL --verbose --update --measurement-regex "counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE $A2LFILE

# Run target executable with XCP on Ethernet in background
$TARGET  &
# Give target some time to initialize
sleep 2

# Create a A2L template with xcp_client by uploading memmory segments and events via XCP on UDP
# @@@@ TCP hangs for some reason
$XCPCLIENT --help
$XCPCLIENT --log-level=4  --dest-addr=127.0.0.1:5000 --list-mea ".*" --list-cal ".*" --a2l $A2LFILE 




# Stop target
kill $!
# Give target some time to shutdown
sleep 1



