# Queue test


## Test results

Note that the long tail in the lock time histograms on POSIX systems is due to the fact that the OS scheduler may preempt the producer thread in the queue acquire function.  
This is normal behaviour and not a queue performance indicator.  

General conclusion is, that with large payloads, a fixed size queue performs significantly better, while it has larger memory requirements to configure the queue for the desired throughput and latency.  

To eliminate the influence of OS scheduling and preemption on the lock time measurement, the test can be configured to use CLOCK_THREAD_CPUTIME_ID, which measures the CPU time consumed by the producer thread, instead of CLOCK_MONOTONIC_RAW, which measures the wall clock time. CLOCK_THREAD_CPUTIME_ID is much slower and has higher jitter, so default setting is CLOCK_MONOTONIC_RAW.  
See the 2 Raspberry Pi 5 small payload test results below, for prove that the long tail in the lock time histogram is due to OS scheduling and not queue performance.



```C
#define TEST_CLOCK_TYPE CLOCK_THREAD_CPUTIME_ID
```


### Queue test Queue64v on Mac OS and Raspberry Pi 5

#### Small payload on Raspberry Pi 5:

```

----------------------------------------------------------------
Using CLOCK_MONOTONIC_RAW for lock time measurement
Using queue (queue64v.c) with 64 bit variable size entries
Testing peek support
Testing with small payload (max 64 bytes)

THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=64

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=1024
QUEUE_ENTRY_USER_SIZE=1028
QUEUE_MAX_ENTRY_SIZE=1028
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4

Statistics:
Test duration: 8.24 seconds
Messages received: 4913332, bytes received: 334106576, messages lost: 0
Average rates: 596619 msg/s, 39619 kbytes/s
Max queue level: 31%
Producer acquire lock time statistics:
  count=4913580  max=26355ns  avg=173ns (cal=34ns)

Lock time histogram (4913580 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         0    0.00%  
  10-20ns                        0    0.00%  
  20-40ns                        0    0.00%  
  40-80ns                    84713    1.72%  #
  80-120ns                  901813   18.35%  ###################
  120-160ns                1233905   25.11%  ##########################
  160-200ns                1408833   28.67%  ##############################
  200-300ns                1247526   25.39%  ##########################
  300-400ns                  34732    0.71%  
  400-500ns                   1204    0.02%  
  500-600ns                    343    0.01%  
  600-800ns                     32    0.00%  
  800-1000ns                     1    0.00%  
  1000-1500ns                   13    0.00%  
  1500-2000ns                   30    0.00%  
  2000-3000ns                  126    0.00%  
  3000-4000ns                  189    0.00%  
  4000-6000ns                   91    0.00%  
  6000-8000ns                   15    0.00%  
  8000-10000ns                   8    0.00%  
  10000-20000ns                  3    0.00%  
  20000-40000ns                  3    0.00%  
  40000-80000ns                  0    0.00%  
  80000-160000ns                 0    0.00%  
  160000-320000ns                0    0.00%  
  >320000ns                      0    0.00%  

----------------------------------------------------------------
Using CLOCK_THREAD_CPUTIME_ID for lock time measurement
Using queue (queue64v.c) with 64 bit variable size entries
Testing peek support
Testing with small payload (max 64 bytes)

THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=64

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=1024
QUEUE_ENTRY_USER_SIZE=1028
QUEUE_MAX_ENTRY_SIZE=1028
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 110.07 seconds
Messages received: 63315920, bytes received: 4305482560, messages lost: 0
Average rates: 575250 msg/s, 38200 kbytes/s
Max queue level: 90%

Producer acquire lock time statistics:
  count=63316004  max=19250ns  avg=218ns (cal=324ns)

Lock time histogram (63316004 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         0    0.00%  
  10-20ns                        0    0.00%  
  20-40ns                        0    0.00%  
  40-80ns                   206270    0.33%  
  80-120ns                 6617359   10.45%  ##########
  120-160ns               14024530   22.15%  ######################
  160-200ns               10096849   15.95%  ################
  200-300ns               18636460   29.43%  ##############################
  300-400ns               12676498   20.02%  ####################
  400-500ns                1016483    1.61%  #
  500-600ns                  17607    0.03%  
  600-800ns                   6165    0.01%  
  800-1000ns                   936    0.00%  
  1000-1500ns                 1926    0.00%  
  1500-2000ns                 2735    0.00%  
  2000-3000ns                 4532    0.01%  
  3000-4000ns                 5179    0.01%  
  4000-6000ns                 2018    0.00%  
  6000-8000ns                  255    0.00%  
  8000-10000ns                 146    0.00%  
  10000-20000ns                 56    0.00%  
  20000-40000ns                  0    0.00%  
  40000-80000ns                  0    0.00%  
  80000-160000ns                 0    0.00%  
  160000-320000ns                0    0.00%  
  >320000ns   


```

#### Large payload on Raspberry Pi 5:

```

Using CLOCK_THREAD_CPUTIME_ID for lock time measurement
Using queue (queue64v.c) with 64 bit variable size entries
Testing peek support
Testing with big payload (max 1024 bytes)

THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=1024

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=1024
QUEUE_ENTRY_USER_SIZE=1028
QUEUE_MAX_ENTRY_SIZE=1028
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 11.45 seconds
Messages received: 6405191, bytes received: 3519398340, messages lost: 82
Average rates: 559185 msg/s, 300049 kbytes/s
Max queue level: 100%

Producer acquire lock time statistics:
  count=6405353  max=16842ns  avg=780ns (cal=324ns)

Lock time histogram (6405353 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         0    0.00%  
  10-20ns                        0    0.00%  
  20-40ns                        0    0.00%  
  40-80ns                       10    0.00%  
  80-120ns                    6468    0.10%  
  120-160ns                  84512    1.32%  #
  160-200ns                 116535    1.82%  #
  200-300ns                 447718    6.99%  #######
  300-400ns                 587678    9.17%  #########
  400-500ns                 494681    7.72%  #######
  500-600ns                 505105    7.89%  ########
  600-800ns                1125419   17.57%  #################
  800-1000ns               1066300   16.65%  ################
  1000-1500ns              1881723   29.38%  ##############################
  1500-2000ns                82179    1.28%  #
  2000-3000ns                 4770    0.07%  
  3000-4000ns                 1388    0.02%  
  4000-6000ns                  734    0.01%  
  6000-8000ns                   93    0.00%  
  8000-10000ns                  28    0.00%  
  10000-20000ns                 12    0.00%  
  20000-40000ns                  0    0.00%  
  40000-80000ns                  0    0.00%  
  80000-160000ns                 0    0.00%  
  160000-320000ns                0    0.00%  
  >320000ns                      0    0.00%  



```



#### Small payload (less than a cache line) test on Mac OS:

```

-------------------------------------------------------------------------------
Test parameters
Using queue (queue64v.c) with 64 bit variable size entries
Testing peek support

Testing with small payload (max 64 bytes)
Using CLOCK_THREAD_CPUTIME_ID for lock time measurement
THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=64

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=1024
QUEUE_ENTRY_USER_SIZE=1028
QUEUE_MAX_ENTRY_SIZE=1028
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 15.42 seconds
Messages received: 32884646, bytes received: 2236155928, messages lost: 0
Average rates: 2132332 msg/s, 141600 kbytes/s
Max queue level: 91%


Deinitialize queue, queue internal statistics:

Producer spin statistics:
  count=32642640  max_spins=9


Producer acquire lock time statistics:
  count=32884840  max=60727ns  avg=85ns (cal=106ns)

Lock time histogram (32884840 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                   1124305    3.42%  ##
  10-20ns                  9017768   27.42%  ####################
  20-40ns                        0    0.00%  
  40-80ns                 13244100   40.27%  ##############################
  80-120ns                 6693426   20.35%  ###############
  120-160ns                1569220    4.77%  ###
  160-200ns                 400301    1.22%  
  200-300ns                 213010    0.65%  
  300-400ns                 127933    0.39%  
  400-500ns                  49300    0.15%  
  500-600ns                  37604    0.11%  
  600-800ns                 118093    0.36%  
  800-1000ns                 88425    0.27%  
  1000-1500ns                84156    0.26%  
  1500-2000ns                32415    0.10%  
  2000-3000ns                25451    0.08%  
  3000-4000ns                15965    0.05%  
  4000-6000ns                27265    0.08%  
  6000-8000ns                 9940    0.03%  
  8000-10000ns                3113    0.01%  
  10000-20000ns               2639    0.01%  
  20000-40000ns                389    0.00%  
  40000-80000ns                 22    0.00%  
  80000-160000ns                 0    0.00%  
  160000-320000ns                0    0.00%  
  >320000ns                      0    0.00%  

```


#### Large payload on Mac OS:

```
Test parameters
Using queue (queue64v.c) with 64 bit variable size entries
Testing peek support

Testing with big payload (max 1024 bytes)
Using CLOCK_THREAD_CPUTIME_ID for lock time measurement
THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=1024

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=1024
QUEUE_ENTRY_USER_SIZE=1028
QUEUE_MAX_ENTRY_SIZE=1028
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 13.50 seconds
Messages received: 25230225, bytes received: 13862099680, messages lost: 191
Average rates: 1868502 msg/s, 1002539 kbytes/s
Max queue level: 99%


Deinitialize queue, queue internal statistics:

Producer spin statistics:
  count=24909171  max_spins=20


Producer acquire lock time statistics:
  count=25230468  max=61892ns  avg=256ns (cal=108ns)

Lock time histogram (25230468 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                       431    0.00%  
  10-20ns                   405648    1.61%  #
  20-40ns                        0    0.00%  
  40-80ns                  2320639    9.20%  ##########
  80-120ns                 3933808   15.59%  #################
  120-160ns                4449904   17.64%  ####################
  160-200ns                4617398   18.30%  ####################
  200-300ns                6641873   26.32%  ##############################
  300-400ns                1459830    5.79%  ######
  400-500ns                 158321    0.63%  
  500-600ns                  89598    0.36%  
  600-800ns                 246416    0.98%  #
  800-1000ns                280142    1.11%  #
  1000-1500ns               271300    1.08%  #
  1500-2000ns                75146    0.30%  
  2000-3000ns                77712    0.31%  
  3000-4000ns                50110    0.20%  
  4000-6000ns                72991    0.29%  
  6000-8000ns                45385    0.18%  
  8000-10000ns               21812    0.09%  
  10000-20000ns              11673    0.05%  
  20000-40000ns                327    0.00%  
  40000-80000ns                  4    0.00%  
  80000-160000ns                 0    0.00%  
  160000-320000ns                0    0.00%  
  >320000ns                      0    0.00%  
```