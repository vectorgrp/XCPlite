# XCPlite Example multi_thread_demo


Shows measurement in multiple threads.  
Create thread local instances of events and measurements.  
Share a parameter segment among multiple threads.  
Thread safe and consistent access to parameters.  
Experimental code to demonstrate how to create context and spans using the XCP instrumentation API.  




## Performance

With 50us sleep time and 8 threads, the producer acquire lock time statistics look like this (MACbook Pro M3):


Producer acquire lock time statistics:
  count=1404480  max_spins=4  max=67583ns  avg=76ns

Lock time histogram (1404480 events):
  Range                      Count        %  Bar
  --------------------  ----------  -------  ------------------------------
  0-40ns                    120470    8.58%  #####
  40-80ns                   627770   44.70%  ##############################
  80-120ns                  528258   37.61%  #########################
  120-160ns                  64571    4.60%  ###
  160-200ns                  15220    1.08%  
  200-240ns                  18220    1.30%  
  240-280ns                  12839    0.91%  
  280-320ns                   4883    0.35%  
  320-360ns                   2487    0.18%  
  360-400ns                   2632    0.19%  
  400-600ns                   3774    0.27%  
  600-800ns                    726    0.05%  
  800-1000ns                   301    0.02%  
  1000-1500ns                  508    0.04%  
  1500-2000ns                  257    0.02%  
  2000-3000ns                  235    0.02%  
  3000-4000ns                  154    0.01%  
  4000-6000ns                  139    0.01%  
  6000-8000ns                  310    0.02%  
  8000-10000ns                 334    0.02%  
  10000-20000ns                315    0.02%  
  20000-40000ns                 67    0.00%  
  40000-80000ns                 10    0.00%  
