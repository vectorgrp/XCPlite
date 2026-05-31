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



## Benchmarking queue64f.c, tail synchronisation versus entry_header synchronisation

Done on a MacBook Air M2 16G

```

Test parameters
Using queue (queue64f.c) with 64 bit fixed size entries
queue64f sync mode: entry_header release, relaxed tail
Testing peek support

Testing with big payload (max 248 bytes)
Using CLOCK_MONOTONIC_RAW for lock time measurement
THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=248

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=248
QUEUE_ENTRY_USER_SIZE=252
QUEUE_MAX_ENTRY_SIZE=252
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 10.67 seconds
Messages received: 13171118, bytes received: 2127170484, messages lost: 3882
Average rates: 1234519 msg/s, 194705 kbytes/s
Max queue level: 100%

Producer spin statistics:
  count=12882406  max_spins=8

Producer acquire lock time statistics:
  count=13175120  max=1417970ns  avg=339ns (cal=30ns)

Lock time histogram (13175120 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                        14    0.00%  
  10-20ns                    10084    0.08%  
  20-40ns                        0    0.00%  
  40-80ns                  2114761   16.05%  ##################
  80-120ns                 3417317   25.94%  ##############################
  120-160ns                2226034   16.90%  ###################
  160-200ns                 779658    5.92%  ######
  200-300ns                1189690    9.03%  ##########
  300-400ns                1267897    9.62%  ###########
  400-500ns                 398254    3.02%  ###
  500-600ns                 756711    5.74%  ######
  600-800ns                 628699    4.77%  #####
  800-1000ns                 98839    0.75%  
  1000-1500ns                42770    0.32%  
  1500-2000ns                40263    0.31%  
  2000-3000ns                60837    0.46%  
  3000-4000ns                32565    0.25%  
  4000-6000ns                21788    0.17%  
  6000-8000ns                 7073    0.05%  
  8000-10000ns                7766    0.06%  
  10000-20000ns              65599    0.50%  
  20000-40000ns               7934    0.06%  
  40000-80000ns                533    0.00%  
  80000-160000ns                20    0.00%  
  160000-320000ns                7    0.00%  
  >320000ns                      7    0.00%  

/Users/rainer/git/XCPlite-RainerZ % 


--------------------------------------------------------------------------------------

Done:

Test parameters
Using queue (queue64f.c) with 64 bit fixed size entries
queue64f sync mode: tail release/acquire
Testing peek support

Testing with big payload (max 248 bytes)
Using CLOCK_MONOTONIC_RAW for lock time measurement
THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=248

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=248
QUEUE_ENTRY_USER_SIZE=252
QUEUE_MAX_ENTRY_SIZE=252
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4

Statistics:
Test duration: 10.01 seconds
Messages received: 8448803, bytes received: 1364570024, messages lost: 155888
Average rates: 844033 msg/s, 133125 kbytes/s
Max queue level: 100%

Producer spin statistics:
  count=8510343  max_spins=5

Producer acquire lock time statistics:
  count=8604996  max=1882136ns  avg=423ns (cal=30ns)

Lock time histogram (8604996 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                         0    0.00%  
  10-20ns                       43    0.00%  
  20-40ns                        0    0.00%  
  40-80ns                    30595    0.36%  
  80-120ns                  168141    1.95%  #
  120-160ns                 282569    3.28%  ##
  160-200ns                 558737    6.49%  ####
  200-300ns                2549328   29.63%  ######################
  300-400ns                3438520   39.96%  ##############################
  400-500ns                 462965    5.38%  ####
  500-600ns                 389660    4.53%  ###
  600-800ns                 368324    4.28%  ###
  800-1000ns                141271    1.64%  #
  1000-1500ns                62640    0.73%  
  1500-2000ns                58865    0.68%  
  2000-3000ns                31109    0.36%  
  3000-4000ns                19790    0.23%  
  4000-6000ns                17928    0.21%  
  6000-8000ns                 3952    0.05%  
  8000-10000ns                2108    0.02%  
  10000-20000ns              11967    0.14%  
  20000-40000ns               4757    0.06%  
  40000-80000ns               1177    0.01%  
  80000-160000ns               357    0.00%  
  160000-320000ns              140    0.00%  
  >320000ns                     53    0.00%  


```


AI review:

My interpretation: on that MacBook Air run, the result is pretty clearly in favor of the entry_header release / relaxed tail variant.

The important numbers:

entry_header sync:
  1,234,519 msg/s
  194,705 kB/s
  3,882 lost
  acquire avg 339 ns

tail release/acquire:
  844,033 msg/s
  133,125 kB/s
  155,888 lost
  acquire avg 423 ns
So the tail-synchronization variant is roughly:

~32% lower message throughput
~32% lower byte throughput
~25% higher average acquire time
~40x more packet loss
The histogram also tells the same story. With entry_header sync, the hot area is mostly 40-160 ns. With tail sync, the hot area shifts to 200-400 ns. That is a big shape change, not just a few scheduler outliers.

My likely explanation: on Apple Silicon / ARM, the acquire-load on the shared tail cache line is not free. Producers already contend on head, and with the tail-sync variant every producer acquisition also puts stronger ordering pressure on another shared queue-header cache line. In the entry_header variant, tail remains just relaxed capacity metadata, so the producer fast path is lighter.

The packet loss difference is also meaningful, but with one caveat: both runs hit Max queue level: 100%, so the test is intentionally stressing saturation. Once saturated, small producer/consumer timing differences can turn into much larger overrun differences. Still, the direction is consistent with the acquire-time histogram.

So I would read this as:

For this benchmark on the MacBook Air, using tail as the release/acquire reuse fence is measurably slower. The original fixed-size design, where tail is relaxed capacity metadata and entry_header owns the per-slot state, performs better.

I would not overgeneralize from one machine/run, but this result supports your intuition: adding acquire/release semantics to the hot shared tail path seems to cost more than it clarifies or helps.




### Repeated with release build

```

Done:

Test parameters
Release Build
Using queue (queue64f.c) with 64 bit fixed size entries
queue64f sync mode: tail release/acquire
Testing peek support
Testing with big payload (max 248 bytes)
Using CLOCK_MONOTONIC_RAW for lock time measurement

THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=248

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=248
QUEUE_ENTRY_USER_SIZE=252
QUEUE_MAX_ENTRY_SIZE=252
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 9.77 seconds
Messages received: 12727212, bytes received: 2055462908, messages lost: 91
Average rates: 1302347 msg/s, 205401 kbytes/s
Max queue level: 100%

Producer spin statistics:
  count=12469603  max_spins=7

Producer acquire lock time statistics:
  count=12727534  max=1251679ns  avg=200ns (cal=29ns)

Lock time histogram (12727534 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                      2321    0.02%  
  10-20ns                  3054122   24.00%  ##############################
  20-40ns                        0    0.00%  
  40-80ns                  2623676   20.61%  #########################
  80-120ns                 2779703   21.84%  ###########################
  120-160ns                1250031    9.82%  ############
  160-200ns                 519822    4.08%  #####
  200-300ns                 775957    6.10%  #######
  300-400ns                1083686    8.51%  ##########
  400-500ns                 343177    2.70%  ###
  500-600ns                  83508    0.66%  
  600-800ns                  17369    0.14%  
  800-1000ns                 15486    0.12%  
  1000-1500ns                27758    0.22%  
  1500-2000ns                28933    0.23%  
  2000-3000ns                36175    0.28%  
  3000-4000ns                19039    0.15%  
  4000-6000ns                13569    0.11%  
  6000-8000ns                 4256    0.03%  
  8000-10000ns                5176    0.04%  
  10000-20000ns              39590    0.31%  
  20000-40000ns               3865    0.03%  
  40000-80000ns                262    0.00%  
  80000-160000ns                31    0.00%  
  160000-320000ns               11    0.00%  
  >320000ns                     11    0.00%  

--------------------------------------------------------------------------


Test parameters
Release Build
Using queue (queue64f.c) with 64 bit fixed size entries
queue64f sync mode: entry_header release, relaxed tail
Testing peek support

Testing with big payload (max 248 bytes)
Using CLOCK_MONOTONIC_RAW for lock time measurement
THREAD_COUNT=10
THREAD_BURST_SIZE=4
THREAD_DELAY_US=10
THREAD_PAYLOAD_MIN_SIZE=64
THREAD_PAYLOAD_MAX_SIZE=248

Queue parameters:
QUEUE_ENTRY_USER_HEADER_SIZE=4
QUEUE_ENTRY_USER_PAYLOAD_SIZE=248
QUEUE_ENTRY_USER_SIZE=252
QUEUE_MAX_ENTRY_SIZE=252
QUEUE_PAYLOAD_SIZE_ALIGNMENT=4


Statistics:
Test duration: 9.83 seconds
Messages received: 13000117, bytes received: 2099643988, messages lost: 143
Average rates: 1322016 msg/s, 208514 kbytes/s
Max queue level: 100%

Producer spin statistics:
  count=12730097  max_spins=8

Producer acquire lock time statistics:
  count=13000390  max=4517431ns  avg=204ns (cal=28ns)

Lock time histogram (13000390 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-10ns                      2348    0.02%  
  10-20ns                  3205817   24.66%  ##############################
  20-40ns                        0    0.00%  
  40-80ns                  2549726   19.61%  #######################
  80-120ns                 2780242   21.39%  ##########################
  120-160ns                1284872    9.88%  ############
  160-200ns                 552220    4.25%  #####
  200-300ns                 826164    6.35%  #######
  300-400ns                1146228    8.82%  ##########
  400-500ns                 351392    2.70%  ###
  500-600ns                  83134    0.64%  
  600-800ns                  17226    0.13%  
  800-1000ns                 15281    0.12%  
  1000-1500ns                28335    0.22%  
  1500-2000ns                30308    0.23%  
  2000-3000ns                37508    0.29%  
  3000-4000ns                20191    0.16%  
  4000-6000ns                14006    0.11%  
  6000-8000ns                 4353    0.03%  
  8000-10000ns                5555    0.04%  
  10000-20000ns              41136    0.32%  
  20000-40000ns               4094    0.03%  
  40000-80000ns                235    0.00%  
  80000-160000ns                11    0.00%  
  160000-320000ns                3    0.00%  
  >320000ns                      5    0.00%  


```


#### AI review:

Prompt:
i repeated the 2 runs with release builds and copied the results to the queue_test readme.  
This time i took care, that the get_clock calibration is similar.  
The last test had a unusually high calibration value.  
Please compare the results again from readme at line 540.  

Answer:
The release-build result changes the interpretation quite a lot.

In the new runs, the two modes are essentially tied:

tail release/acquire:
  1,302,347 msg/s
  205,401 kB/s
  91 lost
  acquire avg 200 ns
  cal 29 ns

entry_header release, relaxed tail:
  1,322,016 msg/s
  208,514 kB/s
  143 lost
  acquire avg 204 ns
  cal 28 ns
So entry_header mode has about 1.5% higher throughput, while tail mode has slightly lower average acquire time and fewer lost messages. That is mixed, and the differences are small enough that I would treat them as benchmark noise unless repeated many times.

The histograms are also almost identical now. Both have the same hot areas:

10-20 ns: ~24%
40-80 ns: ~20%
80-120 ns: ~21%
300-400 ns: ~8-9%
That is very different from the earlier Debug result, where tail sync shifted the whole distribution toward 200-400 ns. With release optimization and similar clock calibration, that shift disappears.

My updated interpretation:

In optimized release builds on this MacBook Air, there is no strong measurable performance difference between tail release/acquire and entry_header release/relaxed-tail synchronization for queue64f.c.

The earlier Debug result probably overstated the cost of tail acquire/release, likely because -O0 changes the generated code enough that atomic/order overhead and surrounding code shape are not representative.

I would still keep the default as entry_header release, relaxed tail, because it better expresses the fixed-slot design and is marginally ahead on throughput here. But performance is no longer a decisive argument. The release result says: choose the model that is clearer and easier to defend.


Prompt:
i also noted a difference in maximum spincount of the cas loop which might be worth considering

Answer:
Yes, worth noting, but I’d treat it as a secondary signal.

Release build:

tail release/acquire:
  max_spins = 7
  count = 12,469,603

entry_header release, relaxed tail:
  max_spins = 8
  count = 12,730,097
Debug build:

entry_header release, relaxed tail:
  max_spins = 8

tail release/acquire:
  max_spins = 5

So the tail-sync variant shows a slightly lower maximum CAS spin count in both runs. That could mean the producers are a little more spread out in time because the acquire-load on tail adds a small ordering/latency cost before the head CAS. Less simultaneous pressure on head can reduce worst-case CAS retries.

But max spin count alone is fragile: it is a single worst observed event, and values like 5, 7, 8 are all very low. I would not choose the design based on that. It is more useful as a supporting observation:

Tail release/acquire may slightly reduce producer CAS contention by adding a small amount of producer-side serialization before the head CAS, but this does not translate into clearly better throughput in the release benchmark.

For the README interpretation, I’d phrase it like:

The tail-synchronization variant shows a slightly lower maximum CAS spin count, suggesting marginally less producer contention on head. However, throughput and average acquire times are effectively equal in the release build, so this is not a decisive advantage.









