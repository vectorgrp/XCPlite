#pragma once

//-----------------------------------------------------------------------------------------------------
// Calibratable lookup table

/*
An instance of struct LookupTable creates a calibratable curve with axis points
*/

// Uses a typedef
// Note: typedefs with maps or curves with shared axis require CANape 24
// #define CANAPE_24

// Lookup table for arbitrary waveforms
namespace lookup_table {

constexpr size_t kLookupTableSize = 11; // Size of the lookup table

struct LookupTableT {

  public:
    float values[kLookupTableSize]; // Values
#ifdef CANAPE_24
    float lookup_axis[kLookupTableSize]; // Axis
#endif

    // Lookup function - returns the interpolated value for the given input
    float Lookup(float input) const;
    void A2lRegisterTypedef() const;
};

} // namespace lookup_table
