
# Queue test



// Test parameters
// 64 byte payload  * THREAD_COUNT * 1000000/THREAD_DELAY_US = Throughput in byte/s

// Parameters for 2000000 msg/s with 10 threads, 64 byte payload, 10us delay
#define THREAD_COUNT 10                            // Number of threads to create
#define MAX_PRODUCERS 8                            // Max concurrent producer processes (SHM mode); also bounds last_counter[] in single-process mode
#define THREAD_DELAY_US 10                         // Delay in microseconds for the thread loops
#define THREAD_BURST_SIZE 2                        // Acquire and push this many entries in a burst before sleeping
#define THREAD_PAYLOAD_SIZE (4 * sizeof(uint64_t)) // Size of the test payload produced by the threads


On Raspberry Pi 5:

Producer acquire lock time statistics:
  count=6210636  max_spins=0  max=34407ns  avg=181ns

Lock time histogram (6210636 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  80-120ns                  677202   10.90%  ######
  120-160ns                3041621   48.97%  ##############################
  160-200ns                 274267    4.42%  ##
  200-240ns                 166816    2.69%  #
  240-280ns                1651532   26.59%  ################
  280-320ns                 252647    4.07%  ##
  320-360ns                  78341    1.26%  
  360-400ns                  33250    0.54%  
  400-600ns                  33159    0.53%  
  600-800ns                    882    0.01%  
  800-1000ns                    82    0.00%  
  1000-1500ns                   21    0.00%  
  1500-2000ns                   74    0.00%  
  2000-3000ns                  424    0.01%  
  3000-4000ns                  180    0.00%  
  4000-6000ns                  105    0.00%  
  6000-8000ns                   20    0.00%  
  8000-10000ns                   2    0.00%  
  10000-20000ns                  6    0.00%  
  20000-40000ns                  5    0.00%  



On Mac OS:

Producer acquire lock time statistics:
  count=19441424  max_spins=0  max=85959ns  avg=64ns

Lock time histogram (19441424 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                   3279521   16.87%  ##########
  40-80ns                  9142758   47.03%  ##############################
  80-120ns                 3977370   20.46%  #############
  120-160ns                2490348   12.81%  ########
  160-200ns                 358998    1.85%  #
  200-240ns                  61438    0.32%  
  240-280ns                  25566    0.13%  
  280-320ns                  13315    0.07%  
  320-360ns                   8234    0.04%  
  360-400ns                   5224    0.03%  
  400-600ns                  10693    0.06%  
  600-800ns                  17580    0.09%  
  800-1000ns                 13196    0.07%  
  1000-1500ns                18339    0.09%  
  1500-2000ns                 6505    0.03%  
  2000-3000ns                 4163    0.02%  
  3000-4000ns                 1352    0.01%  
  4000-6000ns                 2162    0.01%  
  6000-8000ns                  903    0.00%  
  8000-10000ns                 900    0.00%  
  10000-20000ns               2019    0.01%  
  20000-40000ns                762    0.00%  
  40000-80000ns                 76    0.00%  
  80000-160000ns                 2    0.00%  



