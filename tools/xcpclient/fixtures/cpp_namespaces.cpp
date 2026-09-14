// C++ namespace test fixture for the xcpclient unit tests in src/elf_reader/mod.rs (mod test).
// Covers: struct types with the same name in different namespaces (different and identical size), struct types
// with the same name nested in different classes, a same-named type reused by variables in another namespace,
// a nested namespace, a namespaced type without a name conflict, global variables with the same name in
// different namespaces, and const/volatile qualified variables of same-named types.
//
// cpp_namespaces.elf is built from this file with GCC 12.3.1 (xPack arm-none-eabi), DWARF 5, no libraries:
//   arm-none-eabi-g++ -g -gdwarf-5 -O0 -fdebug-prefix-map=$(pwd)=. -nostdlib -nostartfiles -Wl,-e,main \
//       -Wl,--unresolved-symbols=ignore-all -o cpp_namespaces.elf cpp_namespaces.cpp
//
#include <stdint.h>

// 1. Same type name, different size: 4 bytes vs. 8 bytes
namespace motor_control {
struct Input {
    int32_t speed;
};
Input input = {100};
} // namespace motor_control

namespace valve_control {
struct Input {
    int32_t flow;
    int32_t pressure;
};
Input input = {10, 2};
} // namespace valve_control

// 2. Same type name, same size, different member names
namespace motor_control {
struct State {
    uint32_t rpm;
};
State state = {1};
} // namespace motor_control

namespace valve_control {
struct State {
    uint32_t position;
};
State state = {2};
} // namespace valve_control

// 3. Same type name nested in different classes
class MotorController {
  public:
    struct Params {
        uint16_t gain;
    };
    Params params;
};
MotorController motor_controller = {{3}};

class ValveController {
  public:
    struct Params {
        uint32_t gain;
        uint32_t offset;
    };
    Params params;
};
ValveController valve_controller = {{4, 5}};

// 4. The same type used by variables in another namespace, and nested namespaces
namespace diagnostics {
motor_control::Input last_motor_input = {6};
namespace detail {
struct Input {
    uint8_t raw[3];
};
Input input = {{7, 8, 9}};
} // namespace detail
} // namespace diagnostics

// 5. Namespaced type without a name conflict keeps its plain name
namespace motor_control {
struct Output {
    float torque;
};
Output output = {1.5f};
} // namespace motor_control

// 6. Plain struct outside of any namespace, with a conflicting name inside a namespace
struct Config {
    uint32_t id;
};
Config config = {10};
namespace valve_control {
struct Config {
    uint32_t id;
    uint32_t limit;
};
Config config = {11, 12};
} // namespace valve_control

// 7. const and volatile qualified variables of same-named types: the type of the variable is a qualifier entry without a name of its own
volatile motor_control::Input volatile_motor_input = {13};
const valve_control::Input const_valve_input = {14, 15};

volatile uint32_t g_sink;
int main() {
    g_sink = motor_control::input.speed + valve_control::input.flow + motor_control::state.rpm + valve_control::state.position + motor_controller.params.gain +
             valve_controller.params.gain + diagnostics::last_motor_input.speed + diagnostics::detail::input.raw[0] + (uint32_t)motor_control::output.torque + config.id +
             valve_control::config.limit + volatile_motor_input.speed + const_valve_input.pressure;
    return 0;
}
