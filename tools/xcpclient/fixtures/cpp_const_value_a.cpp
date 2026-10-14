// C++ test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test), with cpp_const_value_b.cpp.
// Covers the symbol lookup of variables without DW_AT_location (resolve_address_from_symbols in debuginfo/dwarf/mod.rs), which
// accepts exact symbol names only:
//   - MOSI: a pin number constant from a header (Arduino pins_arduino.h). GCC describes it with DW_AT_const_value and without
//     location, it has no memory and no symbol. The function spiDetachMOSI has a symbol whose name ends with the variable name,
//     a suffix match measured the code of the function, which crashed the ESP32-S3 target (LoadStoreError)
//   - LED_PIN: a constant with the plain name of a global variable in cpp_const_value_b.cpp, which is a different object
//   - sens_value: a variable which is declared but not defined anywhere (no location, no symbol), read_sens_value ends with its name
//   - the XCP_COMMENT marker in namespace motor and the XCP_LIMITS markers in function foo: GCC gives them a DW_AT_const_value
//     and no location, although they are in memory, their symbols are found by the Itanium mangled names
//     (_ZN5motorL24xcp_meta__comment__inputE, _ZZ3foovE29xcp_meta__min__static_counter)
//   - the XCP_COMMENT markers in main and in the extern "C" function spiDetachMOSI: the functions have no mangled name, the
//     markers are mangled with the plain function name (_ZZ4mainE31xcp_meta__comment__main_counter)
//
// cpp_const_value.elf is built from both files with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-g++ -g -gdwarf-5 -O1 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o cpp_const_value.elf cpp_const_value_a.cpp cpp_const_value_b.cpp
//
#include <stdint.h>

#define XCP_COMMENT(name, comment) static const char __attribute__((section("xcp_meta"), used)) xcp_meta__comment__##name[] = comment;
#define XCP_LIMITS(name, min, max)                                                                                                                                                 \
    static const double __attribute__((section("xcp_meta"), used)) xcp_meta__min__##name = min;                                                                                   \
    static const double __attribute__((section("xcp_meta"), used)) xcp_meta__max__##name = max

// Constants as in a board header, without memory
static const uint8_t MOSI = 11;
static const uint8_t LED_PIN = 5;

// Declared, defined nowhere
extern volatile uint32_t sens_value;

volatile uint8_t pin_state = 0;

extern "C" void spiDetachMOSI(void) {
    XCP_COMMENT(detach_counter, "Static local in an extern C function");
    static volatile uint8_t detach_counter = 0;
    detach_counter++;
    pin_state = MOSI;
}

extern "C" uint32_t read_sens_value(void) { return sens_value; }

namespace motor {
XCP_COMMENT(input, "Motor input");
volatile uint16_t input = 0;
} // namespace motor

void foo(void) {
    XCP_LIMITS(static_counter, 0, 1000);
    static volatile uint16_t static_counter = 0;
    static_counter++;
}

int main(void) {
    XCP_COMMENT(main_counter, "Static local in main");
    static volatile uint16_t main_counter = 0;
    main_counter++;
    spiDetachMOSI();
    pin_state = (uint8_t)(pin_state + LED_PIN + read_sens_value() + motor::input);
    foo();
    return 0;
}
