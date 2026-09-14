// C test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Covers the metadata markers (XCP_COMMENT in inc/xcplib.h) of local variables:
//   - a marker at file scope and a marker of the same name in a function (counter): each one belongs to its own variable
//   - the plain marker form in a function (task), the scope prefixed form in another function (foo)
//   - the same marker name in two functions (static_counter), told apart by the size of the marker data
// GCC gives a static const variable in a function no DW_AT_location and names its symbol <name>.<number>, the marker
// addresses can only be resolved from the symbol table, see resolve_local_static_addresses in debuginfo/dwarf/mod.rs.
//
// c_meta_markers.elf is built from this file with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-gcc -g -gdwarf-5 -O1 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o c_meta_markers.elf c_meta_markers.c
//
#include <stdint.h>

typedef struct {
    const char *name;
    uint32_t cycle_time_ns;
    uint8_t priority;
    uint8_t res[16 - sizeof(char *) - 4 - 1];
} tXcpEventDescriptor;

#define XCP_COMMENT(name, comment) static const char __attribute__((section("xcp_meta"), used)) xcp_meta__comment__##name[] = comment;
#define DAQ_CREATE_AND_TRIGGER_EVENT(name)                                                                                                                                         \
    static const tXcpEventDescriptor __attribute__((section("xcp_evts"), used)) evt__##name = {#name, 0, 0, {0}};                                                                   \
    static volatile uint16_t __attribute__((used)) trg__AAS__##name = 0;                                                                                                           \
    trg__AAS__##name = (uint16_t)counter

// Global variable with a marker at file scope
XCP_COMMENT(counter, "Global measurement variable");
volatile uint16_t counter = 0;

// Plain marker form: the markers name the variables of this function
void task(void) {
    XCP_COMMENT(static_counter, "Static local measurement variable in thread function task");
    static volatile uint16_t static_counter = 0;
    XCP_COMMENT(counter, "Local measurement variable in thread function task");
    volatile uint32_t counter = 0;
    static_counter++;
    counter = static_counter;
    DAQ_CREATE_AND_TRIGGER_EVENT(task);
}

// Scope prefixed marker form
void foo(void) {
    XCP_COMMENT(static_counter, "Local static measurement variable in function foo");
    static volatile uint16_t static_counter = 0;
    XCP_COMMENT(foo__counter, "Local measurement variable in function foo");
    volatile uint32_t counter = 0;
    static_counter++;
    counter = static_counter;
    DAQ_CREATE_AND_TRIGGER_EVENT(foo);
}

int main(void) {
    task();
    foo();
    return counter;
}
