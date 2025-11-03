tpmspy_bin_src = files(
    'conntrack.c',
    'sinks.c',
    'sockspy.c',
)

tpmspy_bin = executable(
    'tpmspy',
    tpmspy_bin_src,
    include_directories: incdir,
    dependencies: [ev_dep, dl_dep],
    link_with: [tpmspy_lib],
    install: true,

    install_rpath: '$ORIGIN/../$LIB',
    build_rpath: ':'.join(tpmspy_mods),
)
