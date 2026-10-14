from pathlib import Path

Import("env")

build_dir = Path(env.subst("$BUILD_DIR"))
generated_sections = build_dir / "sections.ld"

# ESP-IDF requires .flash.appdesc and .flash.rodata to be adjacent, and
# esptool requires data sharing a flash MMU page to reside in one ELF segment.
# Generate a project-local shadow of sections.ld which collects XCPlite's
# runtime descriptors inside .flash.rodata and keeps named build metadata
# adjacent in the same mapped flash region.

source_sections = next(
    (
        Path(path) / "sections.ld"
        for path in env["LIBPATH"]
        if (Path(path) / "sections.ld").is_file() and (Path(path) / "sections.ld") != generated_sections
    ),
    None,
)
if source_sections is None:
    raise RuntimeError("Could not locate ESP-IDF sections.ld")

marker = "    _flash_rodata_start = ABSOLUTE(.);\n"
xcp_sections = """    

    __start_xcp_cals = ABSOLUTE(.);
    KEEP(*(xcp_cals))
    __stop_xcp_cals = ABSOLUTE(.);

    __start_xcp_evts = ABSOLUTE(.);
    KEEP(*(xcp_evts))
    __stop_xcp_evts = ABSOLUTE(.);
    
"""

# xcpclient finds these sections by name when reading the ELF. Place them
# immediately after .flash.rodata so they remain in the same mapped flash
# region while retaining their individual ELF section identities.
#
# The trailing ALIGN(8) inside each output section is load bearing. esptool only
# merges ELF sections into one image segment when they are contiguous. Without
# the padding, xcp_epk (5 bytes) ends unaligned and the ALIGN(8) of xcp_meta
# leaves a 7 byte hole, which splits the flash rodata into two DROM segments.
# The ESP-IDF bootloader then maps only the last of them, leaving the real
# rodata unmapped, and the firmware crashes and resets on every boot with:
#   E boot: Image contains multiple DROM segments. Only the last one will be mapped.
# Verify with: esptool --chip esp32s3 image-info firmware.bin | grep DROM

named_sections_marker = "  _flash_rodata_align = ALIGNOF(.flash.rodata);\n"
named_sections = """  

  xcp_epk : ALIGN(1)
  {
    KEEP(*(xcp_epk))
    . = ALIGN(8);
  } > default_rodata_seg

  xcp_meta : ALIGN(8)
  {
    KEEP(*(xcp_meta))
    . = ALIGN(8);
  } > default_rodata_seg

"""

sections_text = source_sections.read_text()
if sections_text.count(marker) != 1:
    raise RuntimeError(f"Unexpected ESP-IDF sections.ld layout: {source_sections}")
if sections_text.count(named_sections_marker) != 1:
    raise RuntimeError(f"Unexpected ESP-IDF sections.ld layout: {source_sections}")
generated_text = sections_text.replace(marker, marker + xcp_sections)
generated_text = generated_text.replace(named_sections_marker, named_sections + named_sections_marker)

generated_sections.parent.mkdir(parents=True, exist_ok=True)
if not generated_sections.exists() or generated_sections.read_text() != generated_text:
    generated_sections.write_text(generated_text)

env.Depends("$BUILD_DIR/${PROGNAME}.elf", str(generated_sections))
