// C++ test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Covers the captured local variables of an event trigger in C++, written out as the macro DaqTriggerEventCapture (inc/xcplib.h)
// expands, because the fixture can not include the headers: one struct member per captured variable, named like the variable with
// a trailing underscore, which is what makes the struct valid in C++ (a name must not change its meaning within a class scope).
// A reference and a struct are captured, cases which the C fixture c_captures.c can not cover.
//
// cpp_captures.elf is built from this file with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-g++ -std=c++17 -g -gdwarf-5 -O2 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o cpp_captures.elf cpp_captures.cpp
//
#include <cstdint>

typedef struct {
    const char *name;
    uint32_t cycle_time_ns;
    uint8_t priority;
    uint8_t res[16 - sizeof(char *) - 4 - 1];
} tXcpEventDescriptor;

extern "C" void XcpEventExt_Var(uint16_t event, int count, ...);
extern "C" const uint8_t *xcp_get_frame_addr(void);
extern "C" uint32_t input(void);

struct test_struct {
    uint8_t a;
    int16_t b;
    float c;
};

#define XCP_CAP_MEMBER(x) __typeof__(x) x##_;
#define XCP_CAP_COPY(c, x) __builtin_memcpy((void *)&(c).x##_, (const void *)&(x), sizeof(x));

volatile uint16_t global_counter = 0;

// A parameter, a reference, a volatile and a struct variable are captured
__attribute__((noinline)) void task(uint32_t param) {
    uint32_t counter = input();      // stays in a register
    volatile float ratio = 0.5f;     // volatile, on the stack
    test_struct s = {1, -2, 0.3f};   // struct
    uint32_t &ref = counter;         // reference to a local variable
    ref++;
    {
        struct {
            XCP_CAP_MEMBER(counter) XCP_CAP_MEMBER(ratio) XCP_CAP_MEMBER(s) XCP_CAP_MEMBER(ref) XCP_CAP_MEMBER(param)
        } cap__task;
        XCP_CAP_COPY(cap__task, counter) XCP_CAP_COPY(cap__task, ratio) XCP_CAP_COPY(cap__task, s) XCP_CAP_COPY(cap__task, ref) XCP_CAP_COPY(cap__task, param)
        static const tXcpEventDescriptor __attribute__((section("xcp_evts"), used)) evt__task = {"task", 0, 0, {0}};
        static volatile uint16_t __attribute__((used)) trg__AASR__task = 0;
        XcpEventExt_Var(trg__AASR__task, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__task);
    }
    global_counter = (uint16_t)counter;
}

int main(void) {
    task(7);
    return global_counter;
}
