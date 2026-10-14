//-----------------------------------------------------------------------------
// Module filter
// The variable selection given on the command line: --elf-unit-limit-min, --elf-unit-limit, --elf-unit-filter and --elf-var-filter
//
// The filter is built and validated once from the command line (ElfFilter::new) and then carried by DebugData, so none of the
// functions of the ELF reader needs a filter parameter and the regexes are compiled exactly once. There is one predicate per
// question, used by everybody who asks it:
//
//   unit_in_range     - --elf-unit-limit only: is the DWARF of this compilation unit read at all? A processing time and memory
//                       limit, used by the DWARF reader while it iterates the units and wherever the definitions of a variable
//                       name are counted
//   unit_is_selected  - can a variable of this compilation unit become an A2L object? unit_in_range plus --elf-unit-filter.
//                       ElfReader::register_variables skips the variable, the DWARF reader uses the same answer to decide
//                       whether a problem with a variable is worth a warning (see log_location_problem in dwarf/attributes.rs)
//   var_is_selected   - is the variable name selected by --elf-var-filter?

use regex::Regex;

use super::debuginfo::make_simple_unit_name_from;

/// The compilation unit and variable name selection of the ELF reader, see the module comment
#[derive(Debug)]
pub struct ElfFilter {
    unit_min: usize,           // --elf-unit-limit-min, the first compilation unit to read
    unit_max: usize,           // --elf-unit-limit, the last compilation unit to read
    unit_regex: Option<Regex>, // --elf-unit-filter, None if no filter is given: all units are selected
    var_regex: Option<Regex>,  // --elf-var-filter, None if no filter is given: all variable names are selected
}

/// No restriction: all compilation units, all variable names
impl Default for ElfFilter {
    fn default() -> Self {
        ElfFilter {
            unit_min: 0,
            unit_max: usize::MAX,
            unit_regex: None,
            var_regex: None,
        }
    }
}

impl ElfFilter {
    /// Build the filter from the command line options, an invalid regex is rejected here and nowhere else
    pub fn new(unit_min: usize, unit_max: usize, unit_filter: &str, var_filter: &str) -> Result<ElfFilter, String> {
        // Compile a filter pattern, an empty pattern means no filter. option is the command line option for the error message
        let compile = |pattern: &str, option: &str| -> Result<Option<Regex>, String> {
            if pattern.is_empty() {
                return Ok(None);
            }
            match Regex::new(pattern) {
                Ok(regex) => Ok(Some(regex)),
                Err(e) => Err(format!("Invalid {} regex '{}': {}", option, pattern, e)),
            }
        };

        Ok(ElfFilter {
            unit_min,
            unit_max,
            unit_regex: compile(unit_filter, "--elf-unit-filter")?,
            var_regex: compile(var_filter, "--elf-var-filter")?,
        })
    }

    /// Log which restrictions are active
    pub fn log(&self) {
        if self.unit_min != 0 || self.unit_max != usize::MAX {
            log::info!("Compilation unit limit: {} ..= {}", self.unit_min, self.unit_max);
        }
        if let Some(regex) = &self.unit_regex {
            log::info!("Compilation unit filter: '{}'", regex.as_str());
        }
        if let Some(regex) = &self.var_regex {
            log::info!("Variable name filter: '{}'", regex.as_str());
        }
    }

    /// The last compilation unit to read (--elf-unit-limit). The unit indices increase monotonically while the DWARF is read,
    /// so the reader can stop entirely once this one is passed
    pub(crate) fn unit_max(&self) -> usize {
        self.unit_max
    }

    /// Is this compilation unit inside --elf-unit-limit-min ..= --elf-unit-limit? Needs no unit name and allocates nothing
    pub(crate) fn unit_in_range(&self, unit_idx: usize) -> bool {
        unit_idx >= self.unit_min && unit_idx <= self.unit_max
    }

    /// Can a variable of this compilation unit become an A2L object? The unit limit plus the --elf-unit-filter regex.
    /// unit_name is the DW_AT_name of the unit as it is in the DWARF, the simple unit name the filter is matched against is
    /// derived here, a unit without a name is matched against its index
    pub(crate) fn unit_is_selected(&self, unit_idx: usize, unit_name: Option<&str>) -> bool {
        if !self.unit_in_range(unit_idx) {
            return false;
        }
        let Some(regex) = &self.unit_regex else {
            return true; // no filter, all units are selected
        };
        match unit_name {
            Some(unit_name) => regex.is_match(&make_simple_unit_name_from(unit_name)),
            None => regex.is_match(&unit_idx.to_string()),
        }
    }

    /// Is the variable name selected by --elf-var-filter?
    pub(crate) fn var_is_selected(&self, var_name: &str) -> bool {
        self.var_regex.as_ref().is_none_or(|regex| regex.is_match(var_name))
    }
}
