// Unit tests of the ELF reader (module elf_reader), the test module of mod.rs
//
// Most tests load one of the ELF files in fixtures/ and check what the reader registers from it. The fixtures are small programs
// with the marker variables and sections of the XCPlite instrumentation macros, built for an embedded target with GCC or clang,
// their source files describe how they are built and what they cover. The other tests build the debug data by hand for cases
// which no compiler produces reliably.

use super::*;

// C++ type test fixture, see fixtures/cpp_types.cpp (GCC 12.3 arm-none-eabi, DWARF 5)
const CPP_TYPES_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_types.elf");
// C++ namespace test fixture, see fixtures/cpp_namespaces.cpp (GCC 12.3 arm-none-eabi, DWARF 5)
const CPP_NAMESPACES_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_namespaces.elf");
// C++ type name collision fixture from pull request vectorgrp/XCPlite#126, see fixtures/cpp_type_name_collisions.cpp
const CPP_TYPE_NAME_COLLISIONS_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_type_name_collisions.elf");
// C test fixture with two compilation units, see fixtures/c_local_types_a.c (GCC 12.3 arm-none-eabi, DWARF 5)
const C_LOCAL_TYPES_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_local_types.elf");
// C test fixture with captured local variables, see fixtures/c_captures.c (GCC 12.3 arm-none-eabi, DWARF 5, -O2)
const C_CAPTURES_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_captures.elf");
// C++ test fixture with captured local variables, see fixtures/cpp_captures.cpp (GCC 12.3 arm-none-eabi, DWARF 5, -O2)
const CPP_CAPTURES_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/cpp_captures.elf");
// C test fixture with metadata markers of local variables, see fixtures/c_meta_markers.c (GCC 12.3 arm-none-eabi, DWARF 5, -O1)
const C_META_MARKERS_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_meta_markers.elf");
// C test fixture with an inlined function, see fixtures/c_inlined_function.c, built with GCC 12.3 arm-none-eabi (-O2) and with clang
const C_INLINED_FUNCTION_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_inlined_function.elf");
const C_INLINED_FUNCTION_CLANG_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_inlined_function_clang.elf");
// The same clang build without frame pointer, the frame base of the functions is the stack pointer
const C_INLINED_FUNCTION_CLANG_NOFP_ELF: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/fixtures/c_inlined_function_clang_nofp.elf");

// Load a fixture ELF file and register all its variables
fn load_fixture(elf_file: &str) -> (ElfReader, Registry) {
    let elf_reader = ElfReader::new(elf_file, 0, (0, usize::MAX)).unwrap_or_else(|e| panic!("failed to load {elf_file}: {e}"));
    let mut reg = Registry::new();
    elf_reader
        .register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None)
        .expect("register_variables failed");
    (elf_reader, reg)
}

// Register all variables of the C++ type fixture
fn load_cpp_types() -> Registry {
    load_fixture(CPP_TYPES_ELF).1
}

// Name of the typedef referenced by a measurement instance
fn instance_typedef(reg: &Registry, var_name: &str) -> &'static str {
    reg.instance_list
        .get_instance(var_name, McObjectType::Measurement, None)
        .unwrap_or_else(|| panic!("instance '{var_name}' not registered"))
        .get_typedef_name()
        .unwrap_or_else(|| panic!("instance '{var_name}' does not reference a typedef"))
}

// Names and offsets of the fields of a typedef
fn typedef_fields(reg: &Registry, typedef_name: &str) -> Vec<(&'static str, u16)> {
    let typedef = reg
        .typedef_list
        .find_typedef(typedef_name)
        .unwrap_or_else(|| panic!("typedef '{typedef_name}' not registered"));
    typedef.fields.iter().map(|f| (f.get_name(), f.offset)).collect()
}

// Struct types with the same name in different namespaces get namespace qualified typedef names and the instances reference them,
// the same type used by several variables is registered once
#[test]
fn test_register_same_named_types_in_namespaces() {
    let (_, reg) = load_fixture(CPP_NAMESPACES_ELF);

    // different size
    assert_eq!(instance_typedef(&reg, "motor_control.input"), "motor_control.Input");
    assert_eq!(instance_typedef(&reg, "valve_control.input"), "valve_control.Input");
    assert_eq!(reg.typedef_list.find_typedef("motor_control.Input").unwrap().size, 4);
    assert_eq!(reg.typedef_list.find_typedef("valve_control.Input").unwrap().size, 8);
    assert_eq!(typedef_fields(&reg, "motor_control.Input"), vec![("speed", 0)]);
    assert_eq!(typedef_fields(&reg, "valve_control.Input"), vec![("flow", 0), ("pressure", 4)]);
    assert!(reg.typedef_list.find_typedef("Input").is_none());

    // same size, different member names
    assert_eq!(instance_typedef(&reg, "motor_control.state"), "motor_control.State");
    assert_eq!(instance_typedef(&reg, "valve_control.state"), "valve_control.State");
    assert_eq!(typedef_fields(&reg, "motor_control.State"), vec![("rpm", 0)]);
    assert_eq!(typedef_fields(&reg, "valve_control.State"), vec![("position", 0)]);

    // nested namespace, and the same type used by a variable in another namespace
    assert_eq!(instance_typedef(&reg, "diagnostics.detail.input"), "diagnostics.detail.Input");
    assert_eq!(typedef_fields(&reg, "diagnostics.detail.Input"), vec![("raw", 0)]);
    assert_eq!(instance_typedef(&reg, "last_motor_input"), "motor_control.Input");
    assert_eq!(reg.typedef_list.iter().filter(|t| t.name.as_str().ends_with("Input")).count(), 3);
}

// The type of a const or volatile qualified variable is a qualifier entry without a name of its own,
// the scope for the typedef name is the one of the named type behind the qualifier
#[test]
fn test_register_qualified_variables_of_same_named_types() {
    let (_, reg) = load_fixture(CPP_NAMESPACES_ELF);

    assert_eq!(instance_typedef(&reg, "volatile_motor_input"), "motor_control.Input");
    assert_eq!(instance_typedef(&reg, "const_valve_input"), "valve_control.Input");
    assert!(reg.typedef_list.find_typedef("Input").is_none());
}

// Equal sized and differently sized types with the same name in two namespaces, fixture of pull request vectorgrp/XCPlite#126
#[test]
fn test_register_namespaced_types_with_colliding_names() {
    let (_, reg) = load_fixture(CPP_TYPE_NAME_COLLISIONS_ELF);

    for (instance_name, typedef_name) in [
        ("g_namespace_1_type_a", "namespace_1.TypeA"),
        ("g_namespace_2_type_a", "namespace_2.TypeA"),
        ("g_namespace_1_type_b", "namespace_1.TypeB"),
        ("g_namespace_2_type_b", "namespace_2.TypeB"),
    ] {
        assert_eq!(instance_typedef(&reg, instance_name), typedef_name, "{instance_name}");
    }
    assert_eq!(reg.typedef_list.len(), 4);
    assert_eq!(typedef_fields(&reg, "namespace_1.TypeA"), vec![("member_1", 0)]);
    assert_eq!(typedef_fields(&reg, "namespace_2.TypeA"), vec![("member_1", 0), ("member_2", 4)]);
    assert_eq!(typedef_fields(&reg, "namespace_1.TypeB"), vec![("member_1", 0), ("member_2", 4)]);
    assert_eq!(typedef_fields(&reg, "namespace_2.TypeB"), vec![("member_3", 0), ("member_4", 4)]);
}

// File local struct types with the same tag but different content in two C compilation units:
// there is no scope to qualify the name with, the second type gets a numeric suffix
#[test]
fn test_register_same_named_c_types_in_different_units() {
    let (_, reg) = load_fixture(C_LOCAL_TYPES_ELF);

    assert_eq!(instance_typedef(&reg, "state_a"), "state");
    assert_eq!(instance_typedef(&reg, "state_b"), "state_1");
    assert_eq!(typedef_fields(&reg, "state"), vec![("a", 0)]);
    assert_eq!(typedef_fields(&reg, "state_1"), vec![("b1", 0), ("b2", 4)]);
}

// A non-zero lower unit index limit (--elf-unit-limit-min) must only skip the compilation units below it, not stop parsing
// entirely: state_a is declared in compilation unit 0 (excluded by the min limit), state_b in compilation unit 1 (still in range)
#[test]
fn test_unit_idx_limit_min_does_not_stop_parsing() {
    let elf_reader = ElfReader::new(C_LOCAL_TYPES_ELF, 0, (1, usize::MAX)).expect("failed to load fixtures/c_local_types.elf");
    let mut reg = Registry::new();
    elf_reader.register_variables(&mut reg, false, 0, (1, usize::MAX), "", "", None).unwrap();

    assert!(
        reg.instance_list.get_instance("state_a", McObjectType::Measurement, None).is_none(),
        "state_a is in compilation unit 0, below the unit limit minimum, it must not be registered"
    );
    assert!(
        reg.instance_list.get_instance("state_b", McObjectType::Measurement, None).is_some(),
        "state_b is in compilation unit 1, inside the unit limit range, it must still be registered"
    );
}

// Struct types with the same name nested in different classes get class qualified typedef names
#[test]
fn test_register_same_named_nested_types() {
    let (_, reg) = load_fixture(CPP_NAMESPACES_ELF);

    let motor = reg.typedef_list.find_typedef(instance_typedef(&reg, "motor_controller")).unwrap();
    assert_eq!(motor.find_field("params").unwrap().get_typedef_name(), Some("MotorController.Params"));
    let valve = reg.typedef_list.find_typedef(instance_typedef(&reg, "valve_controller")).unwrap();
    assert_eq!(valve.find_field("params").unwrap().get_typedef_name(), Some("ValveController.Params"));
    assert_eq!(typedef_fields(&reg, "MotorController.Params"), vec![("gain", 0)]);
    assert_eq!(typedef_fields(&reg, "ValveController.Params"), vec![("gain", 0), ("offset", 4)]);
    assert!(reg.typedef_list.find_typedef("Params").is_none());
}

// Types with a unique name keep their plain name, also inside a namespace.
// A type outside of any namespace keeps its plain name when a namespaced type has the same name.
#[test]
fn test_register_unique_type_names_unchanged() {
    let (_, reg) = load_fixture(CPP_NAMESPACES_ELF);

    assert_eq!(instance_typedef(&reg, "output"), "Output");
    assert_eq!(instance_typedef(&reg, "config"), "Config");
    assert_eq!(instance_typedef(&reg, "valve_control.config"), "valve_control.Config");
    assert_eq!(reg.typedef_list.find_typedef("Config").unwrap().size, 4);
    assert_eq!(reg.typedef_list.find_typedef("valve_control.Config").unwrap().size, 8);
}

// A function with an event trigger which the compiler inlined (and also emitted out of line) has no stack frame layout which is
// valid for all its copies: its stack relative variables are not registered, in none of the copies. The static variables
// keep the function scope and get the event of the trigger, like in a function which is not inlined.
// GCC describes the static variables and the markers in the abstract instance, clang in the out of line copy
#[test]
fn test_register_inlined_function_variables() {
    // GCC: frame base CFA, all locals frame base relative. clang: frame base r11, counter frame base relative, test_float
    // stack pointer relative and therefore not measurable
    for (elf_file, bar_counter_offset, test_float_measurable) in [(C_INLINED_FUNCTION_ELF, -24, true), (C_INLINED_FUNCTION_CLANG_ELF, -4, false)] {
        register_inlined_function_variables(elf_file, bar_counter_offset, test_float_measurable);
    }
}

// The checks of test_register_inlined_function_variables for one fixture ELF file (GCC or clang build of the same source),
// bar_counter_offset is the DW_OP_fbreg offset of the variable counter in bar
fn register_inlined_function_variables(elf_file: &str, bar_counter_offset: i64, test_float_measurable: bool) {
    let elf_reader = ElfReader::new(elf_file, 0, (0, usize::MAX)).unwrap_or_else(|e| panic!("failed to load {elf_file}: {e}"));
    let mut reg = Registry::new();
    elf_reader.register_events(&mut reg, 0).unwrap();
    elf_reader.register_event_locations(&mut reg, 0).unwrap();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    let event_id = |name: &str| reg.event_list.find_event(name, 0).unwrap_or_else(|| panic!("event {name}")).get_id();
    let instance = |name: &str| reg.instance_list.get_instance(name, McObjectType::Measurement, None);

    // bar is not inlined: stack and static variables with the event triggered in bar. The stack variables are addressed with
    // their DWARF offset from the frame base of bar, the CFA for GCC and the frame pointer for clang, both without correction
    let bar_counter = instance("bar.counter").expect("bar.counter");
    assert_eq!(bar_counter.event_id(), Some(event_id("bar")));
    let (addr_ext, addr) = bar_counter.get_address().get_a2l_addr(&reg);
    assert_eq!(addr_ext, 2, "{elf_file}");
    assert_eq!(
        (addr & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK) as i64 - McAddress::XCP_ADDR_EXT_DYN_OFFSET_OFFSET as i64,
        bar_counter_offset,
        "{elf_file}"
    );
    assert_eq!(instance("bar.test_float").is_some(), test_float_measurable, "{elf_file}");
    assert_eq!(instance("bar.static_counter").expect("bar.static_counter").event_id(), Some(event_id("bar")));

    // foo is inlined into main and emitted out of line: the trigger marker is flagged, the stack variables of both copies are
    // not registered (in particular not as local variables of main), the static variables keep the scope and the event
    assert!(
        elf_reader.debug_data.variables["trg__AAS__foo"]
            .iter()
            .all(|v| v.inlined && v.function.as_deref() == Some("foo"))
    );
    assert!(!elf_reader.debug_data.variables["trg__AAS__bar"][0].inlined);
    for name in ["foo.counter", "foo.test_float", "main.counter", "main.test_float", "counter", "test_float"] {
        assert!(instance(name).is_none(), "{name} must not be registered");
    }
    assert_eq!(instance("foo.static_counter").expect("foo.static_counter").event_id(), Some(event_id("foo")));
    assert_eq!(elf_reader.debug_data.variables["static_counter"].len(), 2);
}

// A function without frame pointer (clang -O0 without -fno-omit-frame-pointer) describes its local variables relative to the
// stack pointer, which is not the frame address the trigger macro passes: the stack variables are not registered, the static
// variables keep the function scope and the event
#[test]
fn test_register_stack_variables_without_frame_pointer() {
    let elf_reader = ElfReader::new(C_INLINED_FUNCTION_CLANG_NOFP_ELF, 0, (0, usize::MAX)).expect("failed to load fixtures/c_inlined_function_clang_nofp.elf");
    let mut reg = Registry::new();
    elf_reader.register_events(&mut reg, 0).unwrap();
    elf_reader.register_event_locations(&mut reg, 0).unwrap();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    let instance = |name: &str| reg.instance_list.get_instance(name, McObjectType::Measurement, None);

    assert_eq!(elf_reader.debug_data.variables["trg__AAS__bar"][0].frame_base, FrameBase::Register(13));
    assert!(instance("bar.counter").is_none());
    assert!(instance("bar.test_float").is_none());
    let bar_id = reg.event_list.find_event("bar", 0).unwrap().get_id();
    assert_eq!(instance("bar.static_counter").expect("bar.static_counter").event_id(), Some(bar_id));
}

// The captured local variables of an event trigger are registered from the members of the capture struct, with the name of the
// original variable qualified with its function, address extension 3 and the offset of the member as address. A variable which
// is captured is not registered a second time as a stack variable, and a capture in an inlined function works
#[test]
fn test_register_captured_variables() {
    let elf_reader = ElfReader::new(C_CAPTURES_ELF, 0, (0, usize::MAX)).expect("failed to load fixtures/c_captures.elf");
    let mut reg = Registry::new();
    elf_reader.register_events(&mut reg, 0).unwrap();
    elf_reader.register_event_locations(&mut reg, 0).unwrap();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    elf_reader.register_captures(&mut reg, 0).unwrap();

    let event_id = |name: &str| reg.event_list.find_event(name, 0).unwrap_or_else(|| panic!("event {name}")).get_id();
    // (address extension, offset from the base address, event id) of a measurement
    let address = |name: &str| {
        let instance = reg
            .instance_list
            .get_instance(name, McObjectType::Measurement, None)
            .unwrap_or_else(|| panic!("instance '{name}' not registered"));
        let (addr_ext, addr) = instance.get_address().get_a2l_addr(&reg);
        (addr_ext, addr & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK, instance.event_id())
    };

    // The captured variables of task: the offsets are the offsets of the members in the capture struct
    let task = event_id("task");
    assert_eq!(address("task.counter"), (3, 0, Some(task)));
    assert_eq!(address("task.ratio"), (3, 4, Some(task)));
    assert_eq!(address("task.flags"), (3, 8, Some(task)));

    // 'both' is on the stack and captured, the capture wins and it is registered once
    assert_eq!(address("task.both"), (3, 12, Some(task)));
    assert_eq!(reg.instance_list.iter().filter(|i| i.get_name() == "task.both").count(), 1);

    // 'stack_var' is not captured, it keeps its stack frame relative address
    assert_eq!(address("task.stack_var").0, 2);

    // foo is inlined, its captured variable is measurable nevertheless
    let (addr_ext, offset, event) = address("foo.counter");
    assert_eq!((addr_ext, offset), (3, 0));
    assert_eq!(event, Some(event_id("foo")));
}

// The captured local variables of a C++ event trigger: a const parameter and a reference are captured like any other variable,
// a captured struct keeps the name of its own type in the A2L file and not the one of any helper type of the capture macro
#[test]
fn test_register_captured_variables_cpp() {
    let elf_reader = ElfReader::new(CPP_CAPTURES_ELF, 0, (0, usize::MAX)).expect("failed to load fixtures/cpp_captures.elf");
    let mut reg = Registry::new();
    elf_reader.register_events(&mut reg, 0).unwrap();
    elf_reader.register_event_locations(&mut reg, 0).unwrap();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    elf_reader.register_captures(&mut reg, 0).unwrap();

    let task = reg.event_list.find_event("task", 0).expect("event task").get_id();
    let address = |name: &str| {
        let instance = reg
            .instance_list
            .get_instance(name, McObjectType::Measurement, None)
            .unwrap_or_else(|| panic!("instance '{name}' not registered"));
        let (addr_ext, addr) = instance.get_address().get_a2l_addr(&reg);
        (addr_ext, addr & McAddress::XCP_ADDR_EXT_DYN_OFFSET_MASK, instance.event_id())
    };

    assert_eq!(address("task.counter"), (3, 0, Some(task)));
    assert_eq!(address("task.ratio"), (3, 4, Some(task))); // volatile variable
    assert_eq!(address("task.s"), (3, 8, Some(task))); // struct
    assert_eq!(address("task.ref"), (3, 16, Some(task))); // reference to a local variable
    assert_eq!(address("task.param"), (3, 20, Some(task))); // const parameter
    assert_eq!(instance_typedef(&reg, "task.s"), "test_struct");
}

// Metadata markers of local variables: a marker at file scope belongs to the global variable and not to the local variables of
// the same name in functions, a marker in a function to the variable of this function, written in the plain form (task) or with
// the scope prefix (foo). GCC gives the markers in a function no DW_AT_location, their addresses come from the symbol table,
// the two markers named static_counter are told apart by their size
#[test]
fn test_register_metadata_local_variable_markers() {
    let elf_reader = ElfReader::new(C_META_MARKERS_ELF, 0, (0, usize::MAX)).expect("failed to load fixtures/c_meta_markers.elf");
    let mut reg = Registry::new();
    elf_reader.register_events(&mut reg, 0).unwrap();
    elf_reader.register_event_locations(&mut reg, 0).unwrap();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    elf_reader.register_metadata(&mut reg, 0).unwrap();

    let comment = |name: &str| {
        reg.instance_list
            .get_instance(name, McObjectType::Measurement, None)
            .unwrap_or_else(|| panic!("instance '{name}' not registered"))
            .comment()
    };
    assert_eq!(comment("counter"), "Global measurement variable");
    assert_eq!(comment("task.counter"), "Local measurement variable in thread function task");
    assert_eq!(comment("foo.counter"), "Local measurement variable in function foo");
    assert_eq!(comment("task.static_counter"), "Static local measurement variable in thread function task");
    assert_eq!(comment("foo.static_counter"), "Local static measurement variable in function foo");
}

// A marker at file scope which names no instance reaches the field of a typedef instance (delay_us -> params.delay_us),
// but never a local variable of a function (foo.counter), which is named by a marker in that function
#[test]
fn test_register_metadata_file_scope_marker_fallback() {
    let meta_base: u64 = 0xF000;
    let meta: Vec<u8> = b"Delay Counter in foo ".to_vec(); // offsets 0 and 6
    let marker = |addr: u64, function: Option<&str>| {
        vec![VarInfo {
            address: (0, addr),
            typeref: 0,
            unit_idx: 0,
            function: function.map(str::to_string),
            namespaces: Vec::new(),
            inlined: false,
            frame_base: FrameBase::Cfa,
        }]
    };
    let mut debug_data = empty_debug_data();
    debug_data.xcp_meta_data = Some((meta_base, meta));
    debug_data.variables.insert("xcp_meta__comment__delay_us".to_string(), marker(meta_base, None));
    debug_data.variables.insert("xcp_meta__comment__counter".to_string(), marker(meta_base + 6, None));
    debug_data.variables.insert("counter".to_string(), marker(0x30400, Some("foo"))); // makes 'foo' a known function name
    let elf = ElfReader::from_debug_data(debug_data);

    let mut reg = Registry::new();
    let measurement = || McSupportData::new(McObjectType::Measurement);
    let ulong = || McDimType::new(McValueType::Ulong, 1, 1);
    reg.instance_list
        .add_instance("params.delay_us", ulong(), measurement(), McAddress::new_a2l(0x30388, 0))
        .unwrap();
    reg.instance_list
        .add_instance("foo.counter", ulong(), measurement(), McAddress::new_a2l(0x30400, 2))
        .unwrap();
    elf.register_metadata(&mut reg, 0).unwrap();

    let comment = |name: &str| reg.instance_list.get_instance(name, McObjectType::Measurement, None).unwrap().comment();
    assert_eq!(comment("params.delay_us"), "Delay");
    assert_eq!(comment("foo.counter"), "");
}

// The addressing mode signature is read from the symbol table, so it is found even when the debug information of the XCPlite
// library is not parsed (--elf-unit-limit) or the library was built without it. The DWARF variables are the fallback
#[test]
fn test_get_target_signature() {
    assert_eq!(ElfReader::from_debug_data(empty_debug_data()).get_target_signature(), None);

    let mut debug_data = empty_debug_data();
    debug_data.symbol_addresses.insert("XCPLITE__CASDD".to_string(), 0xF840);
    assert_eq!(ElfReader::from_debug_data(debug_data).get_target_signature(), Some("CASDD"));

    let mut debug_data = empty_debug_data();
    debug_data.variables.insert(
        "XCPLITE__ACSDD".to_string(),
        vec![VarInfo {
            address: (0, 0xF840),
            typeref: 0,
            unit_idx: 0,
            function: None,
            namespaces: Vec::new(),
            inlined: false,
            frame_base: FrameBase::Unknown,
        }],
    );
    assert_eq!(ElfReader::from_debug_data(debug_data).get_target_signature(), Some("ACSDD"));
}

// Global variables get the default event when one is specified, otherwise no event
#[test]
fn test_register_variables_default_event() {
    let elf_reader = ElfReader::new(CPP_TYPES_ELF, 0, (0, usize::MAX)).expect("failed to load fixtures/cpp_types.elf");

    let mut reg = Registry::new();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", Some(3)).unwrap();
    let g_plain = reg.instance_list.get_instance("g_plain", McObjectType::Measurement, None).expect("instance g_plain");
    assert_eq!(g_plain.event_id(), Some(3));

    let mut reg = Registry::new();
    elf_reader.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    let g_plain = reg.instance_list.get_instance("g_plain", McObjectType::Measurement, None).expect("instance g_plain");
    assert_eq!(g_plain.event_id(), None);
}

// Empty debug data for hand-made test cases
fn empty_debug_data() -> DebugData {
    DebugData {
        variables: IndexMap::new(),
        types: HashMap::new(),
        typenames: HashMap::new(),
        qualified_type_names: HashMap::new(),
        demangled_names: HashMap::new(),
        unit_names: vec![Some("main.c".to_string())],
        producers: vec![Some("GNU C17 12.3.1".to_string())],
        sections: HashMap::new(),
        symbol_addresses: HashMap::new(),
        epk_string: None,
        epk_addr: 0,
        xcp_meta_data: None,
        is_little_endian: true,
    }
}

// Debug data with two struct types named "state" with different members and without scope (e.g. file local types in two C files)
fn elf_reader_with_conflicting_types() -> ElfReader {
    let make_struct = |offset: usize, unit_idx: usize, member: &str| {
        let member_type = TypeInfo {
            name: Some("uint32_t".to_string()),
            unit_idx,
            datatype: DbgDataType::Uint32,
            dbginfo_offset: offset + 1,
        };
        TypeInfo {
            name: Some("state".to_string()),
            unit_idx,
            datatype: DbgDataType::Struct {
                size: 4,
                is_class: false,
                inheritance: IndexMap::new(),
                members: IndexMap::from([(member.to_string(), (member_type, 0u64))]),
            },
            dbginfo_offset: offset,
        }
    };
    let var_info = |address: u64, typeref: usize, unit_idx: usize| VarInfo {
        address: (0, address),
        typeref,
        unit_idx,
        function: None,
        namespaces: Vec::new(),
        inlined: false,
        frame_base: FrameBase::Cfa,
    };
    let mut debug_data = empty_debug_data();
    debug_data.unit_names = vec![Some("a.c".to_string()), Some("b.c".to_string())];
    debug_data.types.insert(0x10, make_struct(0x10, 0, "a"));
    debug_data.types.insert(0x20, make_struct(0x20, 1, "b"));
    debug_data.typenames.insert("state".to_string(), vec![0x10, 0x20]);
    debug_data.variables.insert("state_a".to_string(), vec![var_info(0x1000, 0x10, 0)]);
    debug_data.variables.insert("state_b".to_string(), vec![var_info(0x2000, 0x20, 1)]);
    ElfReader::from_debug_data(debug_data)
}

// Different types with the same name and without scope get a numeric suffix,
// a type used for measurement and for calibration variables gets two typedefs
#[test]
fn test_register_conflicting_typedef_names() {
    let elf = elf_reader_with_conflicting_types();
    let mut reg = Registry::new();
    elf.register_variables(&mut reg, false, 0, (0, usize::MAX), "", "", None).unwrap();
    assert_eq!(instance_typedef(&reg, "state_a"), "state");
    assert_eq!(instance_typedef(&reg, "state_b"), "state_1");
    assert_eq!(typedef_fields(&reg, "state"), vec![("a", 0)]);
    assert_eq!(typedef_fields(&reg, "state_1"), vec![("b", 0)]);

    // The same type registered again for a measurement reuses the typedef
    let type_info = elf.debug_data.types.get(&0x10).unwrap();
    assert_eq!(
        elf.get_dim_type(&mut reg, type_info, McObjectType::Measurement).value_type,
        McValueType::new_typedef("state")
    );
    // Registered for a characteristic it is a different typedef (TYPEDEF_CHARACTERISTIC instead of TYPEDEF_MEASUREMENT components)
    assert_eq!(
        elf.get_dim_type(&mut reg, type_info, McObjectType::Characteristic).value_type,
        McValueType::new_typedef("state_2")
    );
    let field = reg.typedef_list.find_typedef("state_2").unwrap().find_field("a").unwrap();
    assert_eq!(field.get_mc_support_data().get_object_type(), McObjectType::Characteristic);
    assert_eq!(reg.typedef_list.len(), 3);
}

// Metadata markers for a namespace qualified instance (XCP_COMMENT(motor_control__input, ...)) and for a field of its typedef
#[test]
fn test_register_metadata_namespaced_instance() {
    let meta_base: u64 = 0xF000;
    let meta: Vec<u8> = b"Motor input\0rpm\0".to_vec(); // comment at offset 0, unit at offset 12
    let marker = |addr: u64| {
        vec![VarInfo {
            address: (0, addr),
            typeref: 0,
            unit_idx: 0,
            function: None,
            namespaces: vec!["motor_control".to_string()],
            inlined: false,
            frame_base: FrameBase::Cfa,
        }]
    };
    let mut debug_data = empty_debug_data();
    debug_data.xcp_meta_data = Some((meta_base, meta));
    debug_data.variables.insert("xcp_meta__comment__motor_control__input".to_string(), marker(meta_base));
    debug_data
        .variables
        .insert("xcp_meta__unit__motor_control__input__speed".to_string(), marker(meta_base + 12));
    let elf = ElfReader::from_debug_data(debug_data);

    let mut reg = Registry::new();
    reg.add_typedef("motor_control.Input", 4).unwrap();
    reg.add_typedef_field(
        "motor_control.Input",
        "speed",
        McDimType::new(McValueType::Slong, 1, 1),
        McSupportData::new(McObjectType::Measurement),
        0,
    )
    .unwrap();
    reg.instance_list
        .add_instance(
            "motor_control.input",
            McDimType::new(McValueType::new_typedef("motor_control.Input"), 1, 1),
            McSupportData::new(McObjectType::Measurement),
            McAddress::new_a2l(0x30388, 0),
        )
        .unwrap();
    elf.register_metadata(&mut reg, 0).unwrap();

    let instance = reg.instance_list.get_instance("motor_control.input", McObjectType::Measurement, None).unwrap();
    assert_eq!(instance.comment(), "Motor input");
    let field = reg.typedef_list.find_typedef("motor_control.Input").unwrap().find_field("speed").unwrap();
    assert_eq!(field.get_mc_support_data().get_unit(), "rpm");
}

// Metadata markers without scope prefix and with the same DWARF name in different namespaces are applied to their own instance each
#[test]
fn test_register_metadata_same_marker_name_in_namespaces() {
    let meta_base: u64 = 0xF000;
    let meta: Vec<u8> = b"Motor input\0Valve input\0".to_vec(); // offsets 0 and 12
    let marker = |addr: u64, namespace: &str| VarInfo {
        address: (0, addr),
        typeref: 0,
        unit_idx: 0,
        function: None,
        namespaces: vec![namespace.to_string()],
        inlined: false,
        frame_base: FrameBase::Cfa,
    };
    let mut debug_data = empty_debug_data();
    debug_data.xcp_meta_data = Some((meta_base, meta));
    debug_data.variables.insert(
        "xcp_meta__comment__input".to_string(),
        vec![marker(meta_base, "motor_control"), marker(meta_base + 12, "valve_control")],
    );
    let elf = ElfReader::from_debug_data(debug_data);

    let mut reg = Registry::new();
    let measurement = || McSupportData::new(McObjectType::Measurement);
    let ulong = || McDimType::new(McValueType::Ulong, 1, 1);
    reg.instance_list
        .add_instance("motor_control.input", ulong(), measurement(), McAddress::new_a2l(0x30388, 0))
        .unwrap();
    reg.instance_list
        .add_instance("valve_control.input", ulong(), measurement(), McAddress::new_a2l(0x30390, 0))
        .unwrap();
    elf.register_metadata(&mut reg, 0).unwrap();

    let comment = |name: &str| reg.instance_list.get_instance(name, McObjectType::Measurement, None).unwrap().comment();
    assert_eq!(comment("motor_control.input"), "Motor input");
    assert_eq!(comment("valve_control.input"), "Valve input");
}

// Metadata markers without scope prefix: a marker in the same namespace as the variable refers to the namespace qualified instance,
// a marker in the same function as a local variable refers to the function qualified instance and not to other variables with this name
#[test]
fn test_register_metadata_marker_scope() {
    let meta_base: u64 = 0xF000;
    let meta: Vec<u8> = b"Motor input\0rpm\0Counter in foo\0Static in foo\0Tick in bar\0".to_vec(); // offsets 0, 12, 16, 31 and 45
    let marker = |addr: u64, function: Option<&str>, namespaces: &[&str]| {
        vec![VarInfo {
            address: (0, addr),
            typeref: 0,
            unit_idx: 0,
            function: function.map(str::to_string),
            namespaces: namespaces.iter().map(|s| s.to_string()).collect(),
            inlined: false,
            frame_base: FrameBase::Cfa,
        }]
    };
    let mut debug_data = empty_debug_data();
    debug_data.xcp_meta_data = Some((meta_base, meta));
    debug_data
        .variables
        .insert("xcp_meta__comment__input".to_string(), marker(meta_base, None, &["motor_control"]));
    debug_data
        .variables
        .insert("xcp_meta__unit__input__speed".to_string(), marker(meta_base + 12, None, &["motor_control"]));
    debug_data
        .variables
        .insert("xcp_meta__comment__counter".to_string(), marker(meta_base + 16, Some("foo"), &[]));
    // The static local of foo was removed by the linker (foo is never called), the marker must not attach to fastTask.static_counter
    debug_data
        .variables
        .insert("xcp_meta__comment__static_counter".to_string(), marker(meta_base + 31, Some("foo"), &[]));
    // A static local which is registered without a function prefix (its name is unique)
    debug_data.variables.insert("xcp_meta__comment__tick".to_string(), marker(meta_base + 45, Some("bar"), &[]));
    let elf = ElfReader::from_debug_data(debug_data);

    let mut reg = Registry::new();
    reg.add_typedef("motor_control.Input", 4).unwrap();
    reg.add_typedef_field(
        "motor_control.Input",
        "speed",
        McDimType::new(McValueType::Slong, 1, 1),
        McSupportData::new(McObjectType::Measurement),
        0,
    )
    .unwrap();
    let measurement = || McSupportData::new(McObjectType::Measurement);
    let input_type = || McDimType::new(McValueType::new_typedef("motor_control.Input"), 1, 1);
    let uword = || McDimType::new(McValueType::Uword, 1, 1);
    reg.instance_list
        .add_instance("motor_control.input", input_type(), measurement(), McAddress::new_a2l(0x30388, 0))
        .unwrap();
    reg.instance_list
        .add_instance("valve_control.input", uword(), measurement(), McAddress::new_a2l(0x30390, 0))
        .unwrap();
    reg.instance_list
        .add_instance("foo.counter", uword(), measurement(), McAddress::new_a2l(0x30400, 0))
        .unwrap();
    reg.instance_list
        .add_instance("main.counter", uword(), measurement(), McAddress::new_a2l(0x30402, 0))
        .unwrap();
    reg.instance_list
        .add_instance("fastTask.static_counter", uword(), measurement(), McAddress::new_a2l(0x30404, 0))
        .unwrap();
    reg.instance_list.add_instance("tick", uword(), measurement(), McAddress::new_a2l(0x30406, 0)).unwrap();
    elf.register_metadata(&mut reg, 0).unwrap();

    let comment = |name: &str| reg.instance_list.get_instance(name, McObjectType::Measurement, None).unwrap().comment();
    assert_eq!(comment("motor_control.input"), "Motor input");
    assert_eq!(comment("valve_control.input"), "");
    assert_eq!(comment("foo.counter"), "Counter in foo");
    assert_eq!(comment("main.counter"), "");
    assert_eq!(comment("fastTask.static_counter"), "");
    assert_eq!(comment("tick"), "Tick in bar");
    let field = reg.typedef_list.find_typedef("motor_control.Input").unwrap().find_field("speed").unwrap();
    assert_eq!(field.get_mc_support_data().get_unit(), "rpm");
}

// Variables and struct members of C++ class type are registered like structs
#[test]
fn test_register_class_types() {
    let reg = load_cpp_types();

    for (var_name, typedef_name) in [
        ("g_pubclass", "PubClass"),
        ("g_tpl_class", "TplClass_long_unsigned_int_"),
        ("g_derived_cc", "DerivedCC"),
        ("g_derived_cs", "DerivedCS"),
        ("g_derived_ss", "DerivedSS"),
        ("g_derived_sc", "DerivedSC"),
    ] {
        let inst = reg
            .instance_list
            .get_instance(var_name, McObjectType::Measurement, None)
            .unwrap_or_else(|| panic!("instance '{var_name}' not registered"));
        assert_eq!(inst.dim_type.value_type, McValueType::new_typedef(typedef_name), "{var_name}");
    }

    let pub_class = reg.typedef_list.find_typedef("PubClass").expect("typedef PubClass");
    assert_eq!(pub_class.size, 8);
    assert_eq!(pub_class.find_field("x").map(|f| f.offset), Some(0));
    assert_eq!(pub_class.find_field("y").map(|f| f.offset), Some(4));

    // Inherited members of a class derived from a class are flattened by the DWARF reader
    let derived_cc = reg.typedef_list.find_typedef("DerivedCC").expect("typedef DerivedCC");
    assert_eq!(derived_cc.find_field("cbase_a").map(|f| f.offset), Some(0));
    assert_eq!(derived_cc.find_field("cderived_b").map(|f| f.offset), Some(4));

    // A class typed struct member references the class typedef instead of degrading to UBYTE
    let outer = reg.typedef_list.find_typedef("Outer").expect("typedef Outer");
    let inner_class = outer.find_field("inner_class").expect("field Outer.inner_class");
    assert_eq!(inner_class.dim_type.value_type, McValueType::new_typedef("PubClass"));
    assert_eq!(inner_class.offset, 0);
}

// Base class members are flattened into the derived type for all struct/class combinations
#[test]
fn test_register_inherited_members() {
    let reg = load_cpp_types();

    for (typedef_name, base_member, derived_member) in [
        ("DerivedSS", "base_a", "derived_b"),
        ("DerivedCC", "cbase_a", "cderived_b"),
        ("DerivedCS", "base_a", "cs_b"),
        ("DerivedSC", "cbase_a", "sc_b"),
    ] {
        let typedef = reg
            .typedef_list
            .find_typedef(typedef_name)
            .unwrap_or_else(|| panic!("typedef '{typedef_name}' not registered"));
        assert_eq!(typedef.size, 8, "{typedef_name}");
        assert_eq!(typedef.fields.len(), 2, "{typedef_name} member count");
        assert_eq!(typedef.find_field(base_member).map(|f| f.offset), Some(0), "{typedef_name}.{base_member}");
        assert_eq!(typedef.find_field(derived_member).map(|f| f.offset), Some(4), "{typedef_name}.{derived_member}");
    }
}

// Typedef names which are sanitized (template instantiations) get their members and a matching reference
#[test]
fn test_register_template_struct_members() {
    let reg = load_cpp_types();

    for typedef_name in ["TplStruct_short_unsigned_int_", "TplStruct_float_", "TplClass_long_unsigned_int_"] {
        let typedef = reg
            .typedef_list
            .find_typedef(typedef_name)
            .unwrap_or_else(|| panic!("typedef '{typedef_name}' not registered"));
        assert_eq!(typedef.size, 8, "{typedef_name}");
        assert_eq!(typedef.fields.len(), 2, "{typedef_name} has no members");
        assert_eq!(typedef.find_field("value").map(|f| f.offset), Some(0), "{typedef_name}.value");
        assert_eq!(typedef.find_field("count").map(|f| f.offset), Some(4), "{typedef_name}.count");
    }

    let outer = reg.typedef_list.find_typedef("Outer").unwrap();
    let inner_tpl = outer.find_field("inner_tpl").expect("field Outer.inner_tpl");
    assert_eq!(inner_tpl.dim_type.value_type, McValueType::new_typedef("TplStruct_short_unsigned_int_"));
    assert_eq!(inner_tpl.offset, 8);
}

// Build an ElfReader from hand-made debug data containing only event definition (evt__) and trigger (trg__) marker variables
fn elf_reader_with_markers(markers: &[(&str, u64, &str)], event_section: Option<(u64, u64)>) -> ElfReader {
    let mut debug_data = empty_debug_data();
    for (name, addr, function) in markers {
        debug_data.variables.entry(name.to_string()).or_default().push(VarInfo {
            address: (0, *addr),
            typeref: 0,
            unit_idx: 0,
            function: Some(function.to_string()),
            namespaces: Vec::new(),
            inlined: false,
            frame_base: FrameBase::Cfa,
        });
    }
    if let Some(range) = event_section {
        debug_data.sections.insert("xcp_evts".to_string(), range);
    }
    ElfReader::from_debug_data(debug_data)
}

// An event created in several functions has several definition markers, the first descriptor in the section wins,
// duplicate trigger markers must not panic either
#[test]
fn test_register_events_duplicate_definitions() {
    let elf = elf_reader_with_markers(
        &[
            ("evt__foo", 0x1010, "task_b"),
            ("evt__foo", 0x1000, "task_a"),
            ("evt__bar", 0x1020, "main"),
            ("trg__AAS__foo", 0x2000, "task_a"),
            ("trg__AAS__foo", 0x2004, "task_b"),
        ],
        Some((0x1000, 0x1030)),
    );
    let mut reg = Registry::new();
    elf.register_events(&mut reg, 0).unwrap();
    assert_eq!(reg.event_list.find_event("foo", 0).unwrap().get_id(), 0);
    assert_eq!(reg.event_list.find_event("bar", 0).unwrap().get_id(), 2);
    assert!(reg.event_list.find_event_id(1).is_none());
    elf.register_event_locations(&mut reg, 0).unwrap();
    assert!(reg.event_list.find_event_by_location(0, "task_a").is_some());
}

// Without an event descriptor section every event gets a unique placeholder id (previously all got 0xFFFF and the second one panicked)
#[test]
fn test_register_events_without_descriptor_section() {
    let elf = elf_reader_with_markers(&[("evt__foo", 0x1000, "main"), ("evt__bar", 0x1010, "main"), ("evt__baz", 0, "main")], None);
    let mut reg = Registry::new();
    elf.register_events(&mut reg, 0).unwrap();
    let ids: Vec<u16> = ["foo", "bar", "baz"].iter().map(|n| reg.event_list.find_event(n, 0).unwrap().get_id()).collect();
    assert_eq!(ids, vec![0xFFFF, 0xFFFE, 0xFFFD]);
}

// Markers outside the descriptor section, without address or with an id which is already taken get placeholder ids
#[test]
fn test_register_events_marker_outside_section() {
    let elf = elf_reader_with_markers(
        &[("evt__foo", 0x1000, "main"), ("evt__out", 0x5000, "main"), ("evt__zero", 0, "main")],
        Some((0x1000, 0x1010)),
    );
    let mut reg = Registry::new();
    reg.event_list.add_event(McEvent::new("srv", 0, 0, 0)).unwrap(); // id 0 is taken, e.g. by the XCP server event information
    elf.register_events(&mut reg, 0).unwrap();
    assert_eq!(reg.event_list.find_event("foo", 0).unwrap().get_id(), 0xFFFF);
    assert_eq!(reg.event_list.find_event("out", 0).unwrap().get_id(), 0xFFFE);
    assert_eq!(reg.event_list.find_event("zero", 0).unwrap().get_id(), 0xFFFD);
}
