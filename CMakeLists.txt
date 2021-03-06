cmake_minimum_required(VERSION 3.15)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project("Russian Doom" VERSION 5.0 LANGUAGES C NONE)
set(BUILD_HERETIC_VERSION "5.0")
set(BUILD_HEXEN_VERSION "5.0")
set(BUILD_STRIFE_VERSION "0.0")

# Autotools variables
set(top_srcdir ${CMAKE_CURRENT_SOURCE_DIR})
set(top_builddir ${CMAKE_CURRENT_BINARY_DIR})

# AC_INIT variables
set(PACKAGE_TARNAME "russian-doom")
set(PACKAGE_COPYRIGHT "Copyright \\xA9 2016-2021 Julian Nechaevsky. Licensed under GNU General Public License, version 2")
set(BUILD_DOOM_VERSION "${PROJECT_VERSION}")

# Check architecture
if(WIN32)
    if("${CMAKE_C_COMPILER}" MATCHES "^(.*mingw64.*)|(.*clang64.*)" OR
    (DEFINED MSVC AND "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "x64"))
        set(ARCH x64)
    elseif("${CMAKE_C_COMPILER}" MATCHES "^(.*mingw32.*)|(.*clang32.*)" OR
    (DEFINED MSVC AND "${CMAKE_GENERATOR_PLATFORM}" STREQUAL "Win32"))
        set(ARCH x86)
    endif()
endif()

# Any settings that should apply to all targets in this directory and all
# subdirectories should go here.  Use judiciously.
if(MSVC)
    add_definitions("/D_CRT_SECURE_NO_WARNINGS" "/D_CRT_SECURE_NO_DEPRECATE"
                    "/D_CRT_NONSTDC_NO_DEPRECATE")
    add_compile_options("/wd4068")
    # MSVC already has /O2 in Release flags
else()
    add_compile_options("-Wall" "-Wdeclaration-after-statement"
                        "-Wredundant-decls" "-Wno-unknown-pragmas" "-Wno-unused-result")
    string(REPLACE "-O3" "-O2" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif()

find_package(SDL2 2.0.1 REQUIRED)
find_package(SDL2_mixer 2.0.0 REQUIRED)
find_package(SDL2_net 2.0.0 REQUIRED)

# Check for libsamplerate.
find_package(samplerate)
if(SAMPLERATE_FOUND)
    set(HAVE_LIBSAMPLERATE TRUE)
endif()

# Check for libpng.
find_package(PNG)
if(PNG_FOUND)
    set(HAVE_LIBPNG TRUE)
endif()

find_package(m)

include(CheckSymbolExists)
include(CheckIncludeFile)
check_symbol_exists(strcasecmp "strings.h" HAVE_DECL_STRCASECMP)
check_symbol_exists(strncasecmp "strings.h" HAVE_DECL_STRNCASECMP)
check_symbol_exists(ioperm "sys/io.h" HAVE_IOPERM)
check_symbol_exists(mmap "sys/mman.h" HAVE_MMAP)
check_symbol_exists(i386_iopl "i386/pio.h" HAVE_LIBI386)
check_symbol_exists(amd64_iopl "amd64/pio.h" HAVE_LIBAMD64)

check_include_file("dev/isa/spkrio.h" HAVE_DEV_ISA_SPKRIO_H)
check_include_file("dev/speaker/speaker.h" HAVE_DEV_SPEAKER_SPEAKER_H)
check_include_file("linux/kd.h" HAVE_LINUX_KD_H)

string(REPLACE "." ", " WINDOWS_RC_DOOM_VERSION ${BUILD_DOOM_VERSION})
string(REPLACE "." ", " WINDOWS_RC_HERETIC_VERSION ${BUILD_HERETIC_VERSION})
string(REPLACE "." ", " WINDOWS_RC_HEXEN_VERSION ${BUILD_HEXEN_VERSION})
string(REPLACE "." ", " WINDOWS_RC_STRIFE_VERSION ${BUILD_STRIFE_VERSION})

string(REPLACE " Doom" "" PACKAGE_PREFIX "${PROJECT_NAME}")
string(REPLACE "doom" "" PROGRAM_PREFIX "${PACKAGE_TARNAME}")

if(NOT WIN32)
    Option(DEV_ENV "Development environment" ON)
endif()

configure_file(cmake/config.h.cin config.h)

configure_file(src/doom/resource.rc.in src/doom/resource.rc)
configure_file(src/heretic/resource.rc.in src/heretic/resource.rc)
configure_file(src/hexen/resource.rc.in src/hexen/resource.rc)
configure_file(src/strife/resource.rc.in src/strife/resource.rc)

# [Dasperal] Disable "setup" from compilation
# configure_file(src/setup/doom-resource.rc.in src/setup/doom-resource.rc)
# configure_file(src/setup/heretic-resource.rc.in src/setup/heretic-resource.rc)
# configure_file(src/setup/hexen-resource.rc.in src/setup/hexen-resource.rc)
# configure_file(src/setup/strife-resource.rc.in src/setup/strife-resource.rc)
# configure_file(src/setup/setup-manifest.xml.in src/setup/setup-manifest.xml)

foreach(SUBDIR textscreen midiproc opl pcsound src)
    add_subdirectory("${SUBDIR}")
endforeach()
