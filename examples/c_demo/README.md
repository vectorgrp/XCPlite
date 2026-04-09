# XCPlite Example c_demo

Shows more complex data objects (structs, arrays) and calibration objects (axis, maps and curves).  
Measurement variables on stack and in global memory. 
Asynchronous read (polling) and write to a value on the stack (variable: counter) 
Consistent atomic changes of multiple calibration parameters.  
Calibration page switching and EPK version check.  

Enables log level 4 to observe XCP commands.  
This shows how asynchronous read/write access to a variable on the stack and how the consistent parameter update and measurement work


## Asynchronous access to stack variable

The example demonstrates asynchronous read and write access to a variable 'counter', which is on the stack.  
CANape measurement mode for 'counter' is configured to "Polling every second", which means that CANape will actively read the value from stack during his lifetime in a cyclic manner. The 'counter' has write access and can be modified in the calibration window.



## Consistent parameter changes

Calibration segments support atomic updates, which means that CANape can update multiple parameters at once in a consistent way, while the application will never see an inconsistent state of the parameters during the update.  

The demo has 2 calibration parameters 'test_byte1' and 'test_byte2', which are part of a calibration segment.
The application checks if (test_byte1 == -test_byte2), and if not, it will print a warning message.  

The consistent update is triggered by pressing the update button in the calibration window.  
This "indirect calibration mode" has to be explicitly enabled in the toolbar before.


## Minimum cycle time

This example application is single threaded with a main loop, which has a sleep time defined by the calibration parameter 'delay_us'.
If the sleep time is too low, the application will not be able to keep up continuous measurement, which will lead to queue overruns in the XCP driver or to lost packets, if the UDP transmission cannot keep up.   
The parameter 'delay_us' can be used to evaluate the limit of minimum cycle time.  
XCPlite uses a very efficient lockless queue, so depending on the systems UDP performance and the maximum MTU set, it should be possible to go down to below 10 microseconds.  
The other example multi_thread_demo may be used to evaluate the performance in a multi-threaded application, with contention between different measurement threads.  
On localhost connections on a desktop PC, it should be possible to go down to 1 microsecond measurement cycle time.

For example on a MAC pro M3 running CANape in a VM with 2us sleep time, the DAQ queue producer acquire lock time statistics look like this:

Producer acquire lock time statistics:
  count=3032642  max_spins=0  max=29250ns  avg=27ns

Lock time histogram (3032642 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   1233151   40.66%  #####################
  40-80ns                  1682263   55.47%  ##############################
  80-120ns                   89802    2.96%  #
  120-160ns                  16276    0.54%  
  160-200ns                   3312    0.11%  
  200-240ns                   3308    0.11%  
  240-280ns                   2034    0.07%  
  280-320ns                    921    0.03%  
  320-360ns                    438    0.01%  
  360-400ns                    254    0.01%  
  400-600ns                    575    0.02%  
  600-800ns                    125    0.00%  
  800-1000ns                    44    0.00%  
  1000-1500ns                   33    0.00%  
  1500-2000ns                    8    0.00%  
  2000-3000ns                   17    0.00%  
  3000-4000ns                   11    0.00%  
  4000-6000ns                   12    0.00%  
  6000-8000ns                   25    0.00%  
  8000-10000ns                  17    0.00%  
  10000-20000ns                 14    0.00%  
  20000-40000ns                  2    0.00%  



