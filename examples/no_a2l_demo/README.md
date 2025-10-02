# no_a2l_demo Demo

## Overview
  
Demonstrates how to create an A2L file during the build process or using CANape.  

Create:

@@@@ Does not work, creates TYPEDEF_MEASUREMENT instead of TYPEDEF_CHARACTERISTIC for params

```bash

../a2ltool-RainerZ/target/debug/a2ltool -v --create --measurement-regex "counter"  --characteristic-regex "params" --elffile  examples/no_a2l_demo/CANape/no_a2l_demo.out  --enable-structures --output examples/no_a2l_demo/CANape/no_a2l_demo_test1.a2l    

```

Update:

``bash

../a2ltool-RainerZ/target/debug/a2ltool -v  --elffile  examples/no_a2l_demo/CANape/no_a2l_demo.out  --enable-structures --update FULL --output examples/no_a2l_demo/CANape/no_a2l_demo.a2l

```




