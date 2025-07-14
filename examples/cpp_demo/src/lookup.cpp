//-----------------------------------------------------------------------------------------------------
// Calibratable lookup table

/*
An instance of struct LookupTable creates a calibratable curve with axis points
*/

#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "lookup.hpp"

namespace lookup_table {

void lookup_table::LookupTableT::A2lRegisterTypedef() const {

    // A2l registration of LookupTableT typedef
    A2lOnce(LookupTableT) {
        A2lTypedefBegin(LookupTableT, "A2L typedef for LookupTableT");
        A2lTypedefCurveComponentWithSharedAxis(values, LookupTableT, kLookupTableSize, "Lookup table with shared axis", "", -1.0, 1.0, "axis");
        A2lTypedefAxisComponent(axis, LookupTableT, kLookupTableSize, "Axis for lookup table in", "", -0.0, 1.0);
        A2lTypedefEnd();
    }
}

float lookup_table::LookupTableT::Lookup(float input) const {

    float v = values[kLookupTableSize - 1];
    for (uint8_t i = 0; i < kLookupTableSize - 1; i++) {
        if (input < axis[i + 1]) {
            // Linear interpolation between the two points
            double t1 = axis[i];
            double t2 = axis[i + 1];
            double v1 = values[i];
            double v2 = values[i + 1];
            v = v1 + (v2 - v1) * (input - t1) / (t2 - t1);
            break;
        }
    };
    return v;
}

} // namespace lookup_table
