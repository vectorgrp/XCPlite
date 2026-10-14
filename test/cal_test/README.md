# Calibration Segment Multi-Threading Test (cal_test)

This test demonstrates and validates multi-threaded access to XCP calibration segments using the libxcplite library.

## Purpose

The test creates multiple threads that concurrently access a shared calibration segment created with `CreateCalSeg()`. This validates:

- Thread-safe access to calibration parameters
- Lock-free performance characteristics
- Concurrent read/write operations


## Lock-time comparision of the 2 RCU algorithms under high contention

No visible difference between the 2 cases

But:

#define OPTION_CAL_RCU_REFCOUNT - Publishing does not depend on reader progress
  - Changes are visible with the next lock
  - The lock is lock-free (but not wait-free, retries when a publish happens concurrently)
  - Slightly more complex cache synchronisation
  - Not recommended for micro controller (FreeRTOS) use cases due to potential performance issues
  - Recommended for slow reader, configuration parameter use-cases, not typical calibration use-cases

 #undef OPTION_CAL_RCU_REFCOUNT - Publishing depends on the lock count returning to zero
  - Changes are visible with the first or second first level lock
  - The lock is wait-free

 The reader API is identical, except XcpUnlockCalSeg() needs the page pointer returned by XcpLockCalSeg()
 The C++ API is not affected, the C examples have not been adapted for the API change, the tests are adapted
 #define OPTION_CAL_RCU_REFCOUNT


### #define OPTION_CAL_RCU_REFCOUNT

Reader acquire lock time statistics:
  count=756053  max=36815ns  avg=47ns (cal=19ns)

Reader acquire lock histogram (756053 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                     63212    8.36%  #####
  20-40ns                   293698   38.85%  #########################
  40-80ns                   347244   45.93%  ##############################
  80-120ns                   42096    5.57%  ###
  120-160ns                   5905    0.78%  
  160-200ns                   2013    0.27%  
  200-300ns                   1073    0.14%  



### #undef OPTION_CAL_RCU_REFCOUNT

Reader acquire lock time statistics:
  count=753020  max=25021ns  avg=47ns (cal=20ns)

Reader acquire lock histogram (753020 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                     63124    8.38%  ##### 
  20-40ns                   303061   40.25%  ###########################
  40-80ns                   326085   43.30%  ##############################
  80-120ns                   48328    6.42%  ####
  120-160ns                   8310    1.10%  
  160-200ns                   1968    0.26%  
  



## Detailed Test Results 


### MacBook Pro M3 - #undef OPTION_CAL_RCU_REFCOUNT

Test parameters:
TEST_WRITE_COUNT = 20000
TEST_THREAD_COUNT = 2
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = OFF
TEST_TASK_LOOP_DELAY_US = 1000
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 250
TEST_DATA_SIZE = 8

Thread 0: reads=5461, changes=5103, avg_time=0.06us, max_time=0.54us
Thread 1: reads=5473, changes=5101, avg_time=0.06us, max_time=10.50us

Total Results:
  Total writes: 20000
  Total atomic writes: 0
  Total reads: 10934
  Total changes observed: 10204 (93.3%)
  Total errors: 0
  Average lock time: 0.06 us
  Maximum lock time: 10.50 us

Reader acquire lock time statistics:
  count=10748  max=10480ns  avg=43ns (cal=20ns)

Reader acquire lock histogram (10748 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                       716    6.66%  ###
  20-40ns                     6566   61.09%  ##############################
  40-80ns                     2140   19.91%  #########
  80-120ns                    1056    9.83%  ####
  120-160ns                    108    1.00%  
  160-200ns                     27    0.25%  
  200-300ns                     59    0.55%  
  300-400ns                     65    0.60%  
  400-500ns                      6    0.06%  
  500-600ns                      2    0.02%  
  1000-1500ns                    1    0.01%  
  2000-3000ns                    1    0.01%  
  10000-20000ns                  1    0.01%  


Writer write time statistics:
  count=20000  max=5856ns  avg=47ns (cal=19ns)

Writer write histogram (20000 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                      1080    5.40%  ###
  20-40ns                     9406   47.03%  ##############################
  40-80ns                     7671   38.35%  ########################
  80-120ns                    1394    6.97%  ####
  120-160ns                    275    1.38%  
  160-200ns                     89    0.45%  
  200-300ns                     65    0.33%  
  300-400ns                     13    0.07%  
  400-500ns                      2    0.01%  
  500-600ns                      1    0.01%  
  600-800ns                      2    0.01%  
  800-1000ns                     1    0.01%  
  4000-6000ns                    1    0.01%  


Write to first-observe latency time statistics:
  count=5112  max=4102.23us  avg=668.974us (cal=20ns)

Write to first-observe latency histogram (5112 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-100us                      212    4.15%  #######
  100-200us                    166    3.25%  #####
  200-300us                    217    4.24%  #######
  300-400us                    498    9.74%  #################
  400-500us                    339    6.63%  ###########
  500-600us                    386    7.55%  #############
  600-700us                    875   17.12%  ##############################
  700-800us                    513   10.04%  #################
  800-900us                    544   10.64%  ##################
  900-1000us                   827   16.18%  ############################
  1000-1100us                  283    5.54%  #########
  1100-1200us                  147    2.88%  #####
  1200-1300us                   89    1.74%  ###
  1300-1400us                   15    0.29%  
  >2500us                        1    0.02%  


### MacBook Pro M3 - #define OPTION_CAL_RCU_REFCOUNT


Test parameters:
TEST_WRITE_COUNT = 20000
TEST_THREAD_COUNT = 2
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = OFF
TEST_TASK_LOOP_DELAY_US = 1000
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 250
TEST_DATA_SIZE = 8

Thread 0: reads=5457, changes=5200, avg_time=0.06us, max_time=1.04us
Thread 1: reads=5478, changes=5216, avg_time=0.06us, max_time=0.54us

Total Results:
  Total writes: 20000
  Total atomic writes: 0
  Total reads: 10935
  Total changes observed: 10416 (95.3%)
  Total errors: 0
  Average lock time: 0.06 us
  Maximum lock time: 1.04 us

Reader acquire lock time statistics:
  count=10745  max=1021ns  avg=37ns (cal=20ns)

Reader acquire lock histogram (10745 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                       835    7.77%  ###
  20-40ns                     6774   63.04%  ##############################
  40-80ns                     2383   22.18%  ##########
  80-120ns                     540    5.03%  ##
  120-160ns                     36    0.34%  
  160-200ns                     50    0.47%  
  200-300ns                     93    0.87%  
  300-400ns                     29    0.27%  
  400-500ns                      3    0.03%  
  600-800ns                      1    0.01%  
  1000-1500ns                    1    0.01%  


Writer write time statistics:
  count=20000  max=10521ns  avg=51ns (cal=20ns)

Writer write histogram (20000 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                       746    3.73%  ##
  20-40ns                     7870   39.35%  ########################
  40-80ns                     9613   48.06%  ##############################
  80-120ns                    1236    6.18%  ###
  120-160ns                    249    1.25%  
  160-200ns                    130    0.65%  
  200-300ns                    115    0.57%  
  300-400ns                     34    0.17%  
  400-500ns                      2    0.01%  
  600-800ns                      2    0.01%  
  1000-1500ns                    1    0.01%  
  8000-10000ns                   1    0.01%  
  10000-20000ns                  1    0.01%  


Write to first-observe latency time statistics:
  count=8736  max=481.356us  avg=124.634us (cal=19ns)

Write to first-observe latency histogram (8736 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-100us                     4096   46.89%  ##############################
  100-200us                   2133   24.42%  ###############
  200-300us                   2113   24.19%  ###############
  300-400us                    392    4.49%  ##
  400-500us                      2    0.02%  





## Test Results - Many Threads, fast Readers


### MacBook Pro M3 - #undef OPTION_CAL_RCU_REFCOUNT


Test parameters:
TEST_WRITE_COUNT = 20000
TEST_THREAD_COUNT = 8
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = OFF
TEST_TASK_LOOP_DELAY_US = 50
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 250
TEST_DATA_SIZE = 8

Thread 0: reads=95776, changes=19969, avg_time=0.07us, max_time=11.46us
Thread 1: reads=95901, changes=19981, avg_time=0.07us, max_time=22.29us
Thread 2: reads=95931, changes=19981, avg_time=0.07us, max_time=18.46us
Thread 3: reads=95915, changes=19973, avg_time=0.07us, max_time=16.50us
Thread 4: reads=95961, changes=19982, avg_time=0.07us, max_time=25.04us
Thread 5: reads=95884, changes=19987, avg_time=0.07us, max_time=16.21us
Thread 6: reads=95858, changes=19977, avg_time=0.07us, max_time=15.71us
Thread 7: reads=95929, changes=19975, avg_time=0.07us, max_time=15.12us

Total Results:
  Total writes: 20000
  Total atomic writes: 0
  Total reads: 767155
  Total changes observed: 159825 (20.8%)
  Total errors: 0
  Average lock time: 0.07 us
  Maximum lock time: 25.04 us

Reader acquire lock time statistics:
  count=753020  max=25021ns  avg=47ns (cal=20ns)

Reader acquire lock histogram (753020 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                     63124    8.38%  #####
  10-20ns                       11    0.00%  
  20-40ns                   303061   40.25%  ###########################
  40-80ns                   326085   43.30%  ##############################
  80-120ns                   48328    6.42%  ####
  120-160ns                   8310    1.10%  
  160-200ns                   1968    0.26%  
  200-300ns                   1057    0.14%  
  300-400ns                    330    0.04%  
  400-500ns                     89    0.01%  
  500-600ns                     66    0.01%  
  600-800ns                    113    0.02%  
  800-1000ns                   132    0.02%  
  1000-1500ns                  151    0.02%  
  1500-2000ns                   68    0.01%  
  2000-3000ns                   55    0.01%  
  3000-4000ns                   15    0.00%  
  4000-6000ns                    9    0.00%  
  6000-8000ns                    5    0.00%  
  8000-10000ns                   9    0.00%  
  10000-20000ns                 32    0.00%  
  20000-40000ns                  2    0.00%  


Writer write time statistics:
  count=20000  max=24565ns  avg=115ns (cal=19ns)

Writer write histogram (20000 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         4    0.02%  
  20-40ns                      691    3.46%  ##
  40-80ns                     4891   24.45%  ##############
  80-120ns                   10154   50.77%  ##############################
  120-160ns                   3379   16.89%  #########
  160-200ns                    518    2.59%  #
  200-300ns                    155    0.78%  
  300-400ns                     53    0.27%  
  400-500ns                     22    0.11%  
  500-600ns                     17    0.09%  
  600-800ns                     22    0.11%  
  800-1000ns                    33    0.17%  
  1000-1500ns                   36    0.18%  
  1500-2000ns                    8    0.04%  
  2000-3000ns                    6    0.03%  
  3000-4000ns                    1    0.01%  
  4000-6000ns                    2    0.01%  
  6000-8000ns                    1    0.01%  
  10000-20000ns                  6    0.03%  
  20000-40000ns                  1    0.01%  


Write to first-observe latency time statistics:
  count=20000  max=243.398us  avg=9.194us (cal=19ns)

Write to first-observe latency histogram (20000 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-100us                    19997   99.98%  ##############################
  100-200us                      2    0.01%  
  200-300us                      1    0.01%  


### MacBook Pro M3 - #define OPTION_CAL_RCU_REFCOUNT


Test parameters:
TEST_WRITE_COUNT = 20000
TEST_THREAD_COUNT = 8
TEST_CALBLK = OFF
TEST_ATOMIC_CAL = OFF
TEST_TASK_LOOP_DELAY_US = 50
TEST_TASK_LOCK_DELAY_US = 0
TEST_MAIN_LOOP_DELAY_US = 250
TEST_DATA_SIZE = 8

Thread 0: reads=94979, changes=19996, avg_time=0.06us, max_time=26.38us
Thread 1: reads=95015, changes=19998, avg_time=0.06us, max_time=12.62us
Thread 2: reads=94986, changes=19994, avg_time=0.06us, max_time=11.83us
Thread 3: reads=95051, changes=19990, avg_time=0.06us, max_time=14.83us
Thread 4: reads=94958, changes=19990, avg_time=0.06us, max_time=9.58us
Thread 5: reads=95011, changes=19997, avg_time=0.06us, max_time=13.62us
Thread 6: reads=94928, changes=19997, avg_time=0.06us, max_time=8.71us
Thread 7: reads=95094, changes=19997, avg_time=0.06us, max_time=25.46us

Total Results:
  Total writes: 20000
  Total atomic writes: 0
  Total reads: 760022
  Total changes observed: 159959 (21.0%)
  Total errors: 0
  Average lock time: 0.06 us
  Maximum lock time: 26.38 us

Reader acquire lock time statistics:
  count=746822  max=26355ns  avg=43ns (cal=20ns)

Reader acquire lock histogram (746822 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                     80390   10.76%  #######
  20-40ns                   338475   45.32%  ##############################
  40-80ns                   291481   39.03%  #########################
  80-120ns                   26865    3.60%  ##
  120-160ns                   4349    0.58%  
  160-200ns                   2010    0.27%  
  200-300ns                   1949    0.26%  
  300-400ns                    345    0.05%  
  400-500ns                     26    0.00%  
  500-600ns                     12    0.00%  
  600-800ns                    138    0.02%  
  800-1000ns                   105    0.01%  
  1000-1500ns                  298    0.04%  
  1500-2000ns                  127    0.02%  
  2000-3000ns                  114    0.02%  
  3000-4000ns                   42    0.01%  
  4000-6000ns                   46    0.01%  
  6000-8000ns                   21    0.00%  
  8000-10000ns                  14    0.00%  
  10000-20000ns                 12    0.00%  
  20000-40000ns                  3    0.00%  


Writer write time statistics:
  count=20000  max=14771ns  avg=92ns (cal=21ns)

Writer write histogram (20000 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         1    0.01%  
  20-40ns                     1223    6.12%  ###
  40-80ns                    10760   53.80%  ##############################
  80-120ns                    5334   26.67%  ##############
  120-160ns                   1852    9.26%  #####
  160-200ns                    485    2.42%  #
  200-300ns                    239    1.20%  
  300-400ns                     26    0.13%  
  400-500ns                      6    0.03%  
  600-800ns                      9    0.04%  
  800-1000ns                     9    0.04%  
  1000-1500ns                   23    0.12%  
  1500-2000ns                   12    0.06%  
  2000-3000ns                    9    0.04%  
  3000-4000ns                    4    0.02%  
  4000-6000ns                    3    0.01%  
  6000-8000ns                    1    0.01%  
  8000-10000ns                   2    0.01%  
  10000-20000ns                  2    0.01%  


Write to first-observe latency time statistics:
  count=19999  max=4340.77us  avg=10.957us (cal=19ns)

Write to first-observe latency histogram (19999 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-100us                    19967   99.84%  ##############################
  100-200us                     19    0.10%  
  200-300us                      4    0.02%  
  300-400us                      3    0.02%  
  400-500us                      1    0.01%  
  500-600us                      1    0.01%  
  800-900us                      1    0.01%  
  1600-1700us                    1    0.01%  
  >2500us                        2    0.01%  
