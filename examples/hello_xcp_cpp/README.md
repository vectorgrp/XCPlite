# XCPlite hello_xcp_cpp Examples


# Measurement of instances on heap

```

    // FloatingAverage instance on heap behind a unique_ptr, needs an extra event 'evt_heap' to measure it
    auto average_filter = std::make_unique<floating_average::FloatingAverage<128>>();
    DaqCreateEvent(evt_heap);
    A2lSetRelativeAddrMode(evt_heap, average_filter.get());
    A2lCreateInstance("average_filter", "FloatingAverage", 1, average_filter.get(), "Heap instance of FloatingAverage<128>");
    
...

    // FloatingAverage instance on heap
    // Trigger the event "evt_heap" to measure the 'FloatingAverage' 
    double average_voltage = average_filter->calculate(voltage);
    DaqTriggerEventExt(evt_heap, average_filter.get());




```


