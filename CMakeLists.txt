cmake_minimum_required(VERSION 3.20.2)
project(paq8pxhy LANGUAGES CXX C)
set(CMAKE_POLICY_DEFAULT_CMP0009 NEW)  # Suppress GLOB warning

# --------------------------------------
# Build options
# --------------------------------------
option(NATIVECPU "Whether to build for your CPU (vs. general public)" OFF)
option(DISABLE_ZLIB "Disable zlib re-compression support" OFF)
option(NDEBUG "Suppress asserts and array bound checks" ON)
option(ENABLE_WARNINGS "Enable compiler warnings ('diag' mode)" OFF)
option(ENABLE_OPENZL "Enable the OpenZL v0.2.0 routed expert" ON)
set(OPENZL_SOURCE_DIR "" CACHE PATH
    "Optional clean OpenZL checkout at the approved commit")
set(OPENZL_SOURCE_COMMIT "3dceb64867840201fb8f57a29d179995f700c9b8"
    CACHE STRING "Approved full OpenZL v0.2.0 source commit")

# Configure OpenZL before this directory adds PAQ-specific compiler flags so
# the dependency keeps its own upstream compiler policy.
if (ENABLE_OPENZL)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpenZLDependency.cmake")
    paq_configure_openzl(PAQ_OPENZL_TARGET)
endif()

# --------------------------------------
# Compiler settings
# --------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(PAQ_MSVC_FRONTEND OFF)
if (MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(PAQ_MSVC_FRONTEND ON)
endif()

# Force floating-point reproducibility without passing GNU-only switches to
# MSVC or clang-cl generated Visual Studio projects.
if (PAQ_MSVC_FRONTEND)
    set(SAFEFP_FLAGS /fp:strict)
    set(BASE_FLAGS /O2 /Ob2 ${SAFEFP_FLAGS})
else()
    set(SAFEFP_FLAGS -fno-fast-math -ffp-contract=off)
    set(BASE_FLAGS
        -O3
        -floop-strip-mine
        -funroll-loops
        -ftree-vectorize
        -fgcse-sm
        -falign-loops=16
        ${SAFEFP_FLAGS}
    )
endif()

# Architecture-specific flags
if (NATIVECPU)
    if (PAQ_MSVC_FRONTEND)
        message(WARNING
            "NATIVECPU has no portable MSVC/clang-cl equivalent; using the "
            "toolchain's configured target architecture")
    else()
        list(APPEND BASE_FLAGS -march=native -mtune=native)
    endif()
else()
    if (PAQ_MSVC_FRONTEND)
        # The selected Visual Studio platform already defines the target ISA.
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i.86")
        list(APPEND BASE_FLAGS -march=nocona -mtune=generic)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "armv7l")
        list(APPEND BASE_FLAGS -march=armv7-a -mtune=cortex-a8)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        list(APPEND BASE_FLAGS -march=armv8-a)
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "riscv64")
        list(APPEND BASE_FLAGS -march=rv64gc -mabi=lp64d)
    else()
        message(WARNING "Unknown architecture: ${CMAKE_SYSTEM_PROCESSOR}, using generic optimizations")
    endif()
endif()

add_compile_options(${BASE_FLAGS})

# Warning flags
if (ENABLE_WARNINGS)
    if (PAQ_MSVC_FRONTEND)
        add_compile_options(/W4)
    else()
        add_compile_options(-Wall)
    endif()
    message(STATUS "Warnings enabled (diag mode)")
endif()

# --------------------------------------
# NDEBUG
# --------------------------------------
if (NDEBUG)
    add_compile_definitions(NDEBUG)
endif()

# --------------------------------------
# Zlib handling
# --------------------------------------
if (DISABLE_ZLIB)
    message(STATUS "Zlib disabled by user")
    add_compile_definitions(DISABLE_ZLIB)
else()
    message(STATUS "Building bundled zlib")
    
    # Define zlib source directory
    set(ZLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zlib")
    
    # Collect zlib sources
    set(ZLIB_SOURCES
        ${ZLIB_DIR}/adler32.c
        ${ZLIB_DIR}/crc32.c
        ${ZLIB_DIR}/deflate.c
        ${ZLIB_DIR}/gzlib.c
        ${ZLIB_DIR}/inffast.c
        ${ZLIB_DIR}/inflate.c
        ${ZLIB_DIR}/inftrees.c
        ${ZLIB_DIR}/trees.c
        ${ZLIB_DIR}/zutil.c
    )
    
    # Create static zlib library
    add_library(zlib_bundled STATIC ${ZLIB_SOURCES})
    if (PAQ_MSVC_FRONTEND)
        target_compile_options(zlib_bundled PRIVATE ${SAFEFP_FLAGS})
    else()
        target_compile_options(zlib_bundled PRIVATE -fexceptions ${SAFEFP_FLAGS})
    endif()

    # Add unistd.h for Linux (provides lseek declaration)
    if (UNIX)
        target_compile_options(zlib_bundled PRIVATE -include unistd.h)
    endif()

    target_include_directories(zlib_bundled PUBLIC ${ZLIB_DIR})
    
    # Set variables for consistency
    set(ZLIB_LIBRARIES zlib_bundled)
    set(ZLIB_INCLUDE_DIRS ${ZLIB_DIR})
endif()

# --------------------------------------
# Collect source files
# --------------------------------------
set(PAQ_SOURCES "")

# Directories to scan for cpp files
set(SOURCE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/file"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/filter"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/model"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/text"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/lstm"
)

foreach(DIR ${SOURCE_DIRS})
    file(GLOB DIR_SOURCES "${DIR}/*.cpp")
    list(APPEND PAQ_SOURCES ${DIR_SOURCES})
endforeach()

# --------------------------------------
# Create executable
# --------------------------------------
add_executable(paq8pxhy ${PAQ_SOURCES})

if (ENABLE_OPENZL)
    target_compile_definitions(paq8pxhy PRIVATE PAQ_ENABLE_OPENZL=1)
    target_link_libraries(paq8pxhy PRIVATE ${PAQ_OPENZL_TARGET})
endif()

# Disable RTTI with the active compiler front-end's spelling.
if (PAQ_MSVC_FRONTEND)
    target_compile_options(paq8pxhy PRIVATE /GR-)
else()
    target_compile_options(paq8pxhy PRIVATE -fno-rtti)
endif()

# Link Zlib if available
if (NOT DISABLE_ZLIB)
    target_include_directories(paq8pxhy PRIVATE ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(paq8pxhy PRIVATE ${ZLIB_LIBRARIES})
endif()

# Static linking with GCC on Windows
if (MINGW)
    target_link_options(paq8pxhy PRIVATE -static)
endif()

# Strip symbols in release builds
if (CMAKE_BUILD_TYPE STREQUAL "Release" OR NDEBUG)
    if (NOT PAQ_MSVC_FRONTEND)
        target_link_options(paq8pxhy PRIVATE -s)
    endif()
endif()

# --------------------------------------
# IPO / LTO
# --------------------------------------
include(CheckIPOSupported)
check_ipo_supported(RESULT supported OUTPUT error)
if (supported)
    message(STATUS "IPO / LTO enabled")
    set_property(TARGET paq8pxhy PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    
    # Set MAKE for LTO
    if (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_COMPILER_LAUNCHER "")
        set(ENV{MAKE} "mingw32-make")
    endif()
else()
    message(STATUS "IPO / LTO not supported: <${error}>")
endif()

# --------------------------------------
# Build information
# --------------------------------------
message(STATUS "Build configuration:")
message(STATUS "  NATIVECPU: ${NATIVECPU}")
message(STATUS "  DISABLE_ZLIB: ${DISABLE_ZLIB}")
message(STATUS "  NDEBUG: ${NDEBUG}")
message(STATUS "  ENABLE_WARNINGS: ${ENABLE_WARNINGS}")
message(STATUS "  ENABLE_OPENZL: ${ENABLE_OPENZL}")
message(STATUS "  PAQ_MSVC_FRONTEND: ${PAQ_MSVC_FRONTEND}")
if (ENABLE_OPENZL)
    message(STATUS "  OpenZL target: ${PAQ_OPENZL_TARGET}")
    if (OPENZL_SOURCE_DIR)
        message(STATUS "  OpenZL source override: ${OPENZL_SOURCE_DIR}")
    else()
        message(STATUS "  OpenZL source: facebook/openzl commit ${OPENZL_SOURCE_COMMIT} (FetchContent)")
    endif()
endif()
message(STATUS "  Architecture: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
