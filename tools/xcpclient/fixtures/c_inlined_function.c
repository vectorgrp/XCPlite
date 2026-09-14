// C test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Covers: a function with an event trigger which the compiler inlines and also emits out of line (the out of line copy refers
// to the abstract instance with DW_AT_abstract_origin and has no name of its own, the inlined copy is a DW_TAG_inlined_subroutine
// in main), and a function with an event trigger which is not inlined. The event descriptors and trigger markers are written
// as the DaqCreateAndTriggerEvent macro in inc/xcplib.h emits them.
//
// c_inlined_function.elf is built from this file with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-gcc -g -gdwarf-5 -O2 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o c_inlined_function.elf c_inlined_function.c
// GCC keeps the static variables and the markers of foo in the abstract instance (DW_AT_inline).
//
// c_inlined_function_clang.elf is built with clang (Apple clang 21.0.0), DWARF 5 with indexed addresses and strings,
// and linked with the GCC toolchain the same way. clang keeps the static variables and the markers of foo in the out of line copy.
// With a frame pointer the frame base of the functions is the frame pointer register r11 and counter is frame pointer relative
// (test_float is stack pointer relative, clang addresses each local from the closer register, such locals are not measurable):
//   clang --target=arm-none-eabi -march=armv4t -marm -ffreestanding -g -gdwarf-5 -O0 -fno-omit-frame-pointer -fdebug-prefix-map=$(pwd)=. \
//       -c -o c_inlined_function_clang.o c_inlined_function.c
//   arm-none-eabi-gcc -nostdlib -nostartfiles -Wl,-e,main -Wl,--unresolved-symbols=ignore-all -o c_inlined_function_clang.elf c_inlined_function_clang.o
// c_inlined_function_clang_nofp.elf is the same without a frame pointer, the frame base of the functions is the stack pointer:
//   clang --target=armv7m-none-eabi -ffreestanding -g -gdwarf-5 -O0 -fdebug-prefix-map=$(pwd)=. -c -o c_inlined_function_clang.o c_inlined_function.c
//
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t cycle_time_ns;
    uint8_t priority;
    uint8_t res[16 - sizeof(char *) - 4 - 1];
} tXcpEventDescriptor;

volatile uint16_t global_counter;

// Inlined into main (always_inline), the out of line copy is kept because foo has external linkage
__attribute__((always_inline)) void foo(void) {
    static volatile uint16_t static_counter = 0;
    volatile uint32_t counter = 0;
    volatile float test_float = 0.1f;
    static_counter++;
    counter = global_counter + static_counter;
    test_float = (float)counter;
    static const tXcpEventDescriptor evt__foo __attribute__((section("xcp_evts"), used)) = {"foo", 0, 0, {0}};
    static volatile uint16_t trg__AAS__foo __attribute__((used)) = 0;
    trg__AAS__foo = (uint16_t)counter + (uint16_t)test_float;
}

// Not inlined
__attribute__((noinline)) void bar(void) {
    static volatile uint16_t static_counter = 0;
    volatile uint32_t counter = 0;
    volatile float test_float = 0.2f;
    static_counter++;
    counter = global_counter + static_counter;
    test_float = (float)counter;
    static const tXcpEventDescriptor evt__bar __attribute__((section("xcp_evts"), used)) = {"bar", 0, 0, {0}};
    static volatile uint16_t trg__AAS__bar __attribute__((used)) = 0;
    trg__AAS__bar = (uint16_t)counter + (uint16_t)test_float;
}

int main(void) {
    foo();
    bar();
    return global_counter;
}
