from pathlib import Path

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
xcplite_inc = project_dir.parent.parent / "inc"
xcplite_src = project_dir.parent.parent / "src"

env.Append(CPPPATH=[str(xcplite_inc), str(xcplite_src)])

xcplite_sources = [
    "cal.c",
    "platform.c",
    "queue32.c",
    "xcpappl.c",
    "xcpethserver.c",
    "xcpethtl.c",
    "xcplite.c",
]

env.BuildSources(
    "$BUILD_DIR/xcplite",
    str(xcplite_src),
    src_filter=["+<{}>".format(source) for source in xcplite_sources],
)
