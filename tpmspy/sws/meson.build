sws_bin_srcs = files(
    'sws.c',
)

sws_bin = executable(
    'sws',
    sws_bin_srcs,
    include_directories: incdir,
    link_with: [tpmspy_lib],
)
