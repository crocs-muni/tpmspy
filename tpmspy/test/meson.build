test_srcs = files(
    'main.cpp',
    'strscpy.cpp',
)

test_bin = executable(
    'tests',
    test_srcs,
    include_directories: incdir,
    link_with: [tpmspy_lib],
)

test('tpmspy tests', test_bin)
