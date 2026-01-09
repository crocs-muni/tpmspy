dump_srcs = files(
    'reader.c',
    'options.c',
    'dump.c',
    'fdcache.c',
    'jsonv.c',
)

dump = executable(
    'dump',
    dump_srcs,
    include_directories: [incdir],
    dependencies: [jsonc_dep, sodium_dep],
    link_with: [tpmspy_lib],
    install: false,
)
