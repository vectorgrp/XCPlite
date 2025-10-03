# Target A2L
A2LFILE="examples/no_a2l_demo/CANape/no_a2l_demo_test1.a2l"


# Path to a2ltool
A2LTOOL="../a2ltool-RainerZ/target/debug/a2ltool"

# Path to ELF file
ELFFILE="examples/no_a2l_demo/CANape/no_a2l_demo.out"

# Create new A2L file with selected measurements and characteristics
$A2LTOOL --verbose --create --measurement-regex "counter"  --characteristic-regex "params" --elffile  $ELFFILE  --enable-structures --output $A2LFILE


