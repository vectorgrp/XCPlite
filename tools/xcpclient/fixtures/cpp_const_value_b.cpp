// Second compilation unit of the C++ test fixture cpp_const_value.elf, see cpp_const_value_a.cpp
// A global variable with the plain name of the constant LED_PIN in cpp_const_value_a.cpp
#include <stdint.h>

extern "C" {
volatile uint8_t LED_PIN = 0;
}
