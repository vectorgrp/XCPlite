# XCPlite hello_xcp_cpp Examples


# Measurement of instances on heap

```

    // FloatingAverage instance on heap behind a unique_ptr, needs an extra event 'evt_heap' to measure it
    auto average_filter2 = std::make_unique<floating_average::FloatingAverage<128>>();
    DaqCreateEvent(evt_heap);
    A2lSetRelativeAddrMode(evt_heap, average_filter2.get());
    A2lCreateInstance(average_filter2, FloatingAverage, 1, average_filter2.get(), "Heap instance of FloatingAverage<128>");
    
...

    // FloatingAverage instance on heap
    // Trigger the event "evt_heap" to measure the 'FloatingAverage' 
    double average_voltage2 = average_filter2->calc(voltage);
    DaqTriggerEventExt(evt_heap, average_filter2.get());

```


