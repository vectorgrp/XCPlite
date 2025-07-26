//-----------------------------------------------------------------------------------------------------
// Calibratable lookup table

/*
An instance of struct LookupTable creates a calibratable curve with axis points
*/

#pragma once

#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "a2l.h"
#include "platform.h"
#include "xcplib.h"
#include "xcplib.hpp"

namespace lookup_table {

// Lookup table for arbitrary waveforms
constexpr uint8_t kLookupTableSize = 11; // Size of the lookup table

struct LookupTableT {

  public:
    float values[kLookupTableSize]; // Values
    float axis[kLookupTableSize];   // Axis

    // Lookup function - returns the interpolated value for the given input
    float Lookup(float input) const;
    void A2lRegisterTypedef() const;
};

} // namespace lookup_table
