project('tpmspy', ['c', 'cpp'], meson_version: '>=1.2',
    default_options: [
        'c_std=c23',
        'cpp_std=c++23',
        'warning_level=3',
        'buildtype=debug',
    ]
)

# Dependencies
ev_dep = dependency('libev', required: true)
jsonc_dep = dependency('json-c', required: true)
sodium_dep = dependency('libsodium', required: true)
dl_dep = dependency('dl', required: true)

incdir = include_directories('include')

add_project_arguments('-D_DEFAULT_SOURCE', language: 'c')

tpmspy_mods = []

# Subdirectories
subdir('lib')
subdir('capture')
subdir('dump')
subdir('src')
subdir('swtpm')
subdir('sws')

if get_option('test').allowed()
    subdir('test')
endif
