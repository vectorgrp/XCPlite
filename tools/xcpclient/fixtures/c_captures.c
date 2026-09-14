// C test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Covers the captured local variables of an event trigger: the struct cap__<event> with one member per captured variable, named
// like the variable with a trailing underscore, and the trigger marker trg__AASR__<event>, as the macro DaqTriggerEventCapture
// (inc/xcplib.h) emits them, written out here because the fixture can not include xcplib.h.
//   - task: captured variables which stay in registers, a variable which is only on the stack (stack_var) and a variable which is
//     captured although it is on the stack as well (both), where the capture wins
//   - foo: a capture in a function which the compiler inlines, the captured variables are measurable nevertheless
//
// c_captures.elf is built from this file with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-gcc -g -gdwarf-5 -O2 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o c_captures.elf c_captures.c
//
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t cycle_time_ns;
    uint8_t priority;
    uint8_t res[16 - sizeof(char *) - 4 - 1];
} tXcpEventDescriptor;

extern void XcpEventExt_Var(uint16_t event, int count, ...);
extern const uint8_t *xcp_get_frame_addr(void);
extern uint32_t input(void);

volatile uint16_t global_counter = 0;

// Not inlined, captures counter, ratio, flags and both
__attribute__((noinline)) void task(uint32_t n) {
    uint32_t counter = 0;        // stays in a register, captured
    float ratio = 0.5f;          // stays in a register, captured
    uint8_t flags[4] = {0};      // captured
    volatile uint16_t stack_var; // on the stack, not captured, stack frame relative
    volatile uint32_t both;      // on the stack and captured, the capture wins
    for (uint32_t i = 0; i < n; i++) {
        counter += input();
        ratio *= 1.5f;
        flags[i & 3] ^= 1;
        stack_var = (uint16_t)counter;
        both = counter;
        {
            struct {
                __typeof__(counter) counter_;
                __typeof__(ratio) ratio_;
                __typeof__(flags) flags_;
                __typeof__(both) both_;
            } cap__task;
            __builtin_memcpy((void *)&cap__task.counter_, (const void *)&(counter), sizeof(counter));
            __builtin_memcpy((void *)&cap__task.ratio_, (const void *)&(ratio), sizeof(ratio));
            __builtin_memcpy((void *)&cap__task.flags_, (const void *)&(flags), sizeof(flags));
            __builtin_memcpy((void *)&cap__task.both_, (const void *)&(both), sizeof(both));
            static const tXcpEventDescriptor __attribute__((section("xcp_evts"), used)) evt__task = {"task", 0, 0, {0}};
            static volatile uint16_t __attribute__((used)) trg__AASR__task = 0;
            XcpEventExt_Var(trg__AASR__task, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__task);
        }
    }
    global_counter = stack_var;
}

// Inlined into main, its captured variable is measurable although the stack frame of an inlined function is not
__attribute__((always_inline)) void foo(void) {
    uint32_t counter = input();
    {
        struct {
            __typeof__(counter) counter_;
        } cap__foo;
        __builtin_memcpy((void *)&cap__foo.counter_, (const void *)&(counter), sizeof(counter));
        static const tXcpEventDescriptor __attribute__((section("xcp_evts"), used)) evt__foo = {"foo", 0, 0, {0}};
        static volatile uint16_t __attribute__((used)) trg__AASR__foo = 0;
        XcpEventExt_Var(trg__AASR__foo, 2, xcp_get_frame_addr(), (const uint8_t *)&cap__foo);
    }
}

int main(void) {
    task(3);
    foo();
    return global_counter;
}
