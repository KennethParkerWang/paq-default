include_guard(GLOBAL)

include(FetchContent)

set(_PAQ_APPROVED_OPENZL_COMMIT
    "3dceb64867840201fb8f57a29d179995f700c9b8")

function(_paq_verify_openzl_version source_dir)
    set(_version_header "${source_dir}/include/openzl/zl_version.h")
    if (NOT EXISTS "${_version_header}")
        message(FATAL_ERROR
            "OpenZL source is missing include/openzl/zl_version.h: "
            "'${source_dir}'")
    endif()

    file(READ "${_version_header}" _version_text)
    if (NOT _version_text MATCHES
            "#[ \t]*define[ \t]+ZL_LIBRARY_VERSION_MAJOR[ \t]+0([^0-9]|$)"
        OR NOT _version_text MATCHES
            "#[ \t]*define[ \t]+ZL_LIBRARY_VERSION_MINOR[ \t]+2([^0-9]|$)"
        OR NOT _version_text MATCHES
            "#[ \t]*define[ \t]+ZL_LIBRARY_VERSION_PATCH[ \t]+0([^0-9]|$)")
        message(FATAL_ERROR
            "PAQ routed archives require OpenZL v0.2.0 exactly. "
            "Rejected source tree: '${source_dir}'.")
    endif()
endfunction()

function(_paq_verify_openzl_git_source source_dir expected_commit)
    if (NOT EXISTS "${source_dir}/.git")
        message(FATAL_ERROR
            "OpenZL source directory is not a Git checkout: '${source_dir}'. "
            "OPENZL_SOURCE_DIR only accepts a clean checkout of the approved "
            "full commit; source archives are intentionally unsupported.")
    endif()

    find_package(Git QUIET)
    if (NOT Git_FOUND)
        message(FATAL_ERROR
            "Git is required to verify OPENZL_SOURCE_DIR provenance.")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}" rev-parse --verify HEAD
        RESULT_VARIABLE _head_result
        OUTPUT_VARIABLE _head_commit
        ERROR_VARIABLE _head_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT _head_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to verify OpenZL source commit in '${source_dir}': "
            "${_head_error}")
    endif()
    if (NOT _head_commit STREQUAL "${expected_commit}")
        message(FATAL_ERROR
            "OpenZL source commit '${_head_commit}' is not the approved "
            "commit '${expected_commit}'.")
    endif()

    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${source_dir}"
                status --porcelain --untracked-files=all
        RESULT_VARIABLE _status_result
        OUTPUT_VARIABLE _status_output
        ERROR_VARIABLE _status_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT _status_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to inspect OpenZL source worktree in '${source_dir}': "
            "${_status_error}")
    endif()
    if (NOT _status_output STREQUAL "")
        message(FATAL_ERROR
            "OpenZL source worktree must be clean at '${expected_commit}'. "
            "Local modifications or untracked files were found in "
            "'${source_dir}'.")
    endif()
endfunction()

function(paq_configure_openzl output_target)
    if (NOT OPENZL_SOURCE_COMMIT STREQUAL _PAQ_APPROVED_OPENZL_COMMIT)
        message(FATAL_ERROR
            "OPENZL_SOURCE_COMMIT must remain the approved OpenZL v0.2.0 "
            "commit '${_PAQ_APPROVED_OPENZL_COMMIT}'.")
    endif()
    # This non-cache setting is deliberately local to dependency creation.
    # OpenZL must not inherit a parent project's shared-library preference.
    set(BUILD_SHARED_LIBS OFF)

    # PAQ only needs the OpenZL C library. Keep all optional OpenZL products out
    # of the canonical build, including install rules and developer utilities.
    set(OPENZL_BUILD_ALL OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_PARQUET_TOOLS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_PYTHON_EXT OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_PYTHON_EXT_TESTS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_PYTHON_DEMO OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_CPP OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_CUSTOM_PARSERS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_CLI OFF CACHE BOOL "" FORCE)
    set(OPENZL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OPENZL_ALLOW_INTROSPECTION OFF CACHE BOOL "" FORCE)
    set(OPENZL_INSTALL OFF CACHE BOOL "" FORCE)
    set(OPENZL_CPP_INSTALL OFF CACHE BOOL "" FORCE)

    set(_paq_openzl_source_kind "fetched Git checkout")
    if (OPENZL_SOURCE_DIR)
        get_filename_component(
            _paq_openzl_source_dir
            "${OPENZL_SOURCE_DIR}"
            ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
        )
        if (NOT EXISTS "${_paq_openzl_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR
                "OPENZL_SOURCE_DIR must name an OpenZL source tree containing "
                "CMakeLists.txt: '${_paq_openzl_source_dir}'")
        endif()
        _paq_verify_openzl_git_source(
            "${_paq_openzl_source_dir}"
            "${_PAQ_APPROVED_OPENZL_COMMIT}")

        FetchContent_Declare(
            paq_openzl
            SOURCE_DIR "${_paq_openzl_source_dir}"
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/openzl-build"
        )
        set(_paq_openzl_source_kind "verified local Git checkout")
    else()
        FetchContent_Declare(
            paq_openzl
            GIT_REPOSITORY https://github.com/facebook/openzl.git
            GIT_TAG 3dceb64867840201fb8f57a29d179995f700c9b8
            GIT_PROGRESS TRUE
            GIT_SUBMODULES deps/zstd deps/lz4
            GIT_SUBMODULES_RECURSE FALSE
            UPDATE_DISCONNECTED TRUE
            BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/openzl-build"
        )
    endif()

    # Populate first, but do not execute OpenZL's CMakeLists.txt until source
    # provenance and the exact library version have both been validated.
    FetchContent_GetProperties(paq_openzl)
    if (NOT paq_openzl_POPULATED)
        FetchContent_Populate(paq_openzl)
    endif()

    _paq_verify_openzl_git_source(
        "${paq_openzl_SOURCE_DIR}"
        "${_PAQ_APPROVED_OPENZL_COMMIT}")
    _paq_verify_openzl_version("${paq_openzl_SOURCE_DIR}")
    if (NOT EXISTS "${paq_openzl_SOURCE_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "Validated OpenZL source is missing CMakeLists.txt: "
            "'${paq_openzl_SOURCE_DIR}'.")
    endif()
    message(STATUS
        "OpenZL source accepted (${_paq_openzl_source_kind}): "
        "${paq_openzl_SOURCE_DIR}")

    add_subdirectory(
        "${paq_openzl_SOURCE_DIR}"
        "${paq_openzl_BINARY_DIR}"
        EXCLUDE_FROM_ALL
    )

    # v0.2.0 defines the in-tree target as 'openzl'. Retain the exported alias
    # as a compatibility fallback for source packages that provide it as well.
    if (TARGET openzl)
        set(_paq_openzl_target openzl)
    elseif (TARGET OpenZL::openzl)
        set(_paq_openzl_target OpenZL::openzl)
    else()
        message(FATAL_ERROR
            "OpenZL v0.2.0 did not define the expected target 'openzl' "
            "(or compatibility target 'OpenZL::openzl').")
    endif()

    get_target_property(_paq_openzl_aliased_target
        ${_paq_openzl_target} ALIASED_TARGET)
    if (_paq_openzl_aliased_target)
        set(_paq_openzl_type_target ${_paq_openzl_aliased_target})
    else()
        set(_paq_openzl_type_target ${_paq_openzl_target})
    endif()
    get_target_property(_paq_openzl_target_type
        ${_paq_openzl_type_target} TYPE)
    if (NOT _paq_openzl_target_type STREQUAL "STATIC_LIBRARY")
        message(FATAL_ERROR
            "OpenZL must build as STATIC_LIBRARY, but target "
            "'${_paq_openzl_type_target}' has type "
            "'${_paq_openzl_target_type}'.")
    endif()

    set(${output_target} "${_paq_openzl_target}" PARENT_SCOPE)
endfunction()
