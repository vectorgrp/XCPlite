
# Daq test



// Test parameters

#define THREAD_COUNT 8            // Number of threads to create
#define THREAD_DELAY_US 1000      // Default delay in microseconds for the thread loops, calibration parameter
#define THREAD_DELAY_OFFSET_US 50 // Default offset  added to the delay (* task index) for each thread instance, to create different sampling rates
#define THREAD_TIME_SHIFT_NS                                                                                                                                                       \
    (1000000000 / THREAD_COUNT) // Default time shift in nanoseconds (* task index) for each thread instance, to disturb the sequential time ordering of events

#define TEST_DAQ_EVENT_TIMING



---------------------------------------------------------------------------
On Mac OS:

Measurement not running:
------------------------


Producer acquire lock time statistics:
  count=451546  max=202264ns  avg=26ns (cal=28ns)

Lock time histogram (451546 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                    110817   24.54%  ############
  10-20ns                   265075   58.70%  ##############################
  20-40ns                        0    0.00%  
  40-80ns                    45928   10.17%  #####
  80-120ns                   15154    3.36%  #
  120-160ns                   7159    1.59%  
  160-200ns                   3534    0.78%  
  200-300ns                   2205    0.49%  
  300-400ns                    767    0.17%  
  400-500ns                    211    0.05%  
  500-600ns                    167    0.04%  
  600-800ns                    150    0.03%  
  800-1000ns                   127    0.03%  
  1000-1500ns                   96    0.02%  
  1500-2000ns                   36    0.01%  
  2000-3000ns                   19    0.00%  
  3000-4000ns                   35    0.01%  
  4000-6000ns                   16    0.00%  
  6000-8000ns                    7    0.00%  
  8000-10000ns                   7    0.00%  
  10000-20000ns                 19    0.00%  
  20000-40000ns                 14    0.00%  
  40000-80000ns                  1    0.00%  
  80000-160000ns                 1    0.00%  
  160000-320000ns                1    0.00%  
  >320000ns                      0    0.00%  




Measurement running with 8 threads, 500 delay, and 2.5 MByte/s
--------------------------------------------------------------

Producer acquire lock time statistics:
  count=418221  max=177347ns  avg=355ns (cal=28ns)

Lock time histogram (418221 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                     34967    8.36%  ##############
  10-20ns                    52629   12.58%  #####################
  20-40ns                        0    0.00%  
  40-80ns                    16507    3.95%  ######
  80-120ns                   11441    2.74%  ####
  120-160ns                  15748    3.77%  ######
  160-200ns                  15432    3.69%  ######
  200-300ns                  37902    9.06%  ###############
  300-400ns                  61785   14.77%  #########################
  400-500ns                  43157   10.32%  #################
  500-600ns                  72912   17.43%  ##############################
  600-800ns                  40491    9.68%  ################
  800-1000ns                 10524    2.52%  ####
  1000-1500ns                 3008    0.72%  #
  1500-2000ns                  508    0.12%  
  2000-3000ns                  268    0.06%  
  3000-4000ns                  128    0.03%  
  4000-6000ns                  204    0.05%  
  6000-8000ns                  192    0.05%  
  8000-10000ns                 174    0.04%  
  10000-20000ns                191    0.05%  
  20000-40000ns                 45    0.01%  
  40000-80000ns                  6    0.00%  
  80000-160000ns                 1    0.00%  
  160000-320000ns                1    0.00%  
  >320000ns                      0    0.00%  