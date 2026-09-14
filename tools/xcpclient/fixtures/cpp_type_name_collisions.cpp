// C++ type-name collision fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Contributed by Pamir Mundt with pull request vectorgrp/XCPlite#126 (Fix C++ type name collisions in xcpclient).
// Covers equal-sized and differently-sized types with identical unqualified names in separate namespaces.
//
// cpp_type_name_collisions.elf is built from this file without libraries:
//   arm-none-eabi-g++ -g -gdwarf-5 -O0 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o cpp_type_name_collisions.elf cpp_type_name_collisions.cpp
//
namespace namespace_1 {
struct TypeA { unsigned int member_1; };
struct TypeB { unsigned int member_1; unsigned int member_2; };
}

namespace namespace_2 {
struct TypeA { unsigned int member_1; unsigned int member_2; };
struct TypeB { unsigned int member_3; unsigned int member_4; };
}

namespace_1::TypeA g_namespace_1_type_a;
namespace_2::TypeA g_namespace_2_type_a;
namespace_1::TypeB g_namespace_1_type_b;
namespace_2::TypeB g_namespace_2_type_b;

volatile unsigned int g_collision_sink;
int main() {
    g_collision_sink = g_namespace_1_type_a.member_1 + g_namespace_2_type_a.member_2
                     + g_namespace_1_type_b.member_2 + g_namespace_2_type_b.member_4;
    return 0;
}
