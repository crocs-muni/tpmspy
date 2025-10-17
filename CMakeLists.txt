cmake_minimum_required(VERSION 3.18)

project(sockspy)

# Create 'compile_commands.json' to be used by LSP.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Common flags.
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -std=c23 -pedantic -Wall -Wextra")
add_compile_definitions(-D_DEFAULT_SOURCE)

add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)

# Dependency configuration.
find_package(PkgConfig REQUIRED)

pkg_check_modules(EV REQUIRED libev)
pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(SODIUM REQUIRED libsodium)

# Include headers.
include_directories(${PROJECT_SOURCE_DIR}/include)

# The 'sockspy' library for common testable routines.
file(GLOB sockspy_lib_sources ${PROJECT_SOURCE_DIR}/lib/*.c)
add_library(sockspy ${sockspy_lib_sources})

# The 'json' library from CCAN.
# http://git.ozlabs.org/?p=ccan;a=tree;f=ccan/json
#file(GLOB json_lib_sources ${PROJECT_SOURCE_DIR}/json/*.c)
#add_library(json ${json_lib_sources})

# The 'sockspy' binary.
file(GLOB sockspy_bin_sources ${PROJECT_SOURCE_DIR}/src/*.c)
add_executable(sockspy_bin ${sockspy_bin_sources})
target_link_libraries(sockspy_bin sockspy ${EV_LIBRARIES})
target_include_directories(sockspy_bin PUBLIC ${EV_INCLUDE_DIRS})
target_compile_options(sockspy_bin PUBLIC ${EV_CFLAGS_OTHER})
set_property(TARGET sockspy_bin PROPERTY OUTPUT_NAME sockspy)

# The 'swtpm-gate' binary.
file(GLOB swtpm_gate_sources ${PROJECT_SOURCE_DIR}/swtpm/*.c)
add_executable(swtpm_gate_bin ${swtpm_gate_sources})
set_property(TARGET swtpm_gate_bin PROPERTY OUTPUT_NAME swtpm)

# The 'sws' utility.
file(GLOB util_sws_sources ${PROJECT_SOURCE_DIR}/sws/*.c)
add_executable(util_sws_bin ${util_sws_sources})
target_link_libraries(util_sws_bin sockspy)
set_property(TARGET util_sws_bin PROPERTY OUTPUT_NAME sws)

# The 'dump' utility.
file(GLOB util_dump_sources ${PROJECT_SOURCE_DIR}/dump/*.c)
add_executable(util_dump ${util_dump_sources})
target_link_libraries(util_dump sockspy ${JSONC_LIBRARIES} ${SODIUM_LIBRARIES})
target_include_directories(util_dump PUBLIC ${JSONC_INCLUDE_DIRS}
	${SODIUM_INCLUDE_DIRS})
target_compile_options(util_dump PUBLIC ${JSONC_CFLAGS_OTHER}
	${SODIUM_CFLAGS_OTHER})
set_property(TARGET util_dump PROPERTY OUTPUT_NAME dump)

# The tests binary.
file(GLOB sockspy_test_sources
	${PROJECT_SOURCE_DIR}/test/*.c
	${PROJECT_SOURCE_DIR}/test/*.cpp
)

add_executable(sockspy_test ${sockspy_test_sources})
target_link_libraries(sockspy_test sockspy)
set_property(TARGET sockspy_test PROPERTY OUTPUT_NAME tests)
