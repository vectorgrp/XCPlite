//-----------------------------------------------------------------------------------------------------
// Calibratable lookup table

/*
An instance of struct LookupTable creates a calibratable curve with axis points
*/

#include <cmath>   // for fmod, sin
#include <cstdint> // for uintxx_t

#include "a2l.hpp"    // for xcplib A2l generation application programming interface
#include "xcplib.hpp" // for xcplib application programming interface

#include "lookup.hpp" // for kLookupTableSize, LookupTableT

namespace lookup_table {

#ifndef CANAPE_24
constexpr float lookup_axis[] = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f};
#endif

void lookup_table::LookupTableT::A2lRegisterTypedef() const {

    // A2l global once registration of LookupTableT typedef
    if (A2lOnce()) {
#ifdef CANAPE_24
        // New feature in CANape 24, shared axis in typedefs
        A2lTypedefBegin(LookupTableT, this, "A2L typedef for LookupTableT");
        A2lTypedefCurveComponentWithSharedAxis(values, LookupTableT, kLookupTableSize, "Lookup table with shared axis", "", -1.0, 1.0, "lookup_axis");
        A2lTypedefAxisComponent(lookup_axis, LookupTableT, kLookupTableSize, "Axis for lookup table in", "", -0.0, 1.0);
        A2lTypedefEnd();
#else
        // No support for shared axis in nested typedefs, which means, it is not possible to reference an individual axis in typedef instance
        // Using a fixed axis instead
        A2lTypedefBegin(LookupTableT, this, "A2L typedef for LookupTableT");
        A2lTypedefCurveComponent(values, LookupTableT, kLookupTableSize, "Lookup table", "", -1.0, 1.0);
        A2lTypedefEnd();
#endif
    }
}

float lookup_table::LookupTableT::Lookup(float input) const {

    float v = values[kLookupTableSize - 1];
    for (uint8_t i = 0; i < kLookupTableSize - 1; i++) {
        if (input < lookup_axis[i + 1]) {
            // Linear interpolation between the two points
            double t1 = lookup_axis[i];
            double t2 = lookup_axis[i + 1];
            double v1 = values[i];
            double v2 = values[i + 1];
            v = (float)(v1 + (v2 - v1) * (input - t1) / (t2 - t1));
            break;
        }
    };
    return v;
}

} // namespace lookup_table
