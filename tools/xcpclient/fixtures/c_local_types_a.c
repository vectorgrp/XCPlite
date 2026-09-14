// C test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test), first of two compilation units.
// Covers: file local struct types with the same tag name but different content in two compilation units.
// C has no namespaces, the two types can only be told apart by their content.
//
// c_local_types.elf is built from this file and c_local_types_b.c with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-gcc -g -gdwarf-5 -O0 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o c_local_types.elf c_local_types_a.c c_local_types_b.c
//
struct state {
    unsigned int a;
};
struct state state_a = {1};

unsigned int get_state_b(void);
volatile unsigned int g_sink;
int main(void) {
    g_sink = state_a.a + get_state_b();
    return 0;
}
