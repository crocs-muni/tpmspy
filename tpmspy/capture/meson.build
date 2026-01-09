tpmspy_mod_capture_srcs = files(
    'lib.c',
)

tpmspy_mod_capture = shared_module(
    'capture',
    tpmspy_mod_capture_srcs,
    include_directories: incdir,
    install: true,
)

tpmspy_mods += [meson.current_build_dir()]
