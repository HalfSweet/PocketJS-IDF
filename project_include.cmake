include_guard(GLOBAL)

set(POCKETJS_IDF_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(POCKETJS_IDF_EMBED_TOOL
    "${POCKETJS_IDF_COMPONENT_DIR}/tools/pocket_embed.py"
)
set(POCKETJS_IDF_BUILD_TOOL
    "${POCKETJS_IDF_COMPONENT_DIR}/tools/build_pocket.ts"
)

function(_pocketjs_require_arguments prefix kind)
    foreach(argument TARGET NAME ${kind})
        if(NOT ${prefix}_${argument})
            message(FATAL_ERROR
                "pocketjs_${kind} requires ${argument}."
            )
        endif()
    endforeach()
    if(NOT TARGET "${${prefix}_TARGET}")
        message(FATAL_ERROR
            "PocketJS app target '${${prefix}_TARGET}' does not exist. "
            "Call the helper after idf_component_register()."
        )
    endif()
    if(NOT "${${prefix}_NAME}" MATCHES "^[a-z][a-z0-9_]*$")
        message(FATAL_ERROR
            "PocketJS app NAME must match [a-z][a-z0-9_]*."
        )
    endif()
endfunction()

function(_pocketjs_python out_variable)
    if(DEFINED PYTHON AND EXISTS "${PYTHON}")
        set(${out_variable} "${PYTHON}" PARENT_SCOPE)
        return()
    endif()
    find_program(_pocketjs_python_executable NAMES python3 python)
    if(NOT _pocketjs_python_executable)
        message(FATAL_ERROR
            "PocketJS-IDF requires Python 3 to validate and embed .pocket files."
        )
    endif()
    set(${out_variable} "${_pocketjs_python_executable}" PARENT_SCOPE)
endfunction()

function(_pocketjs_attach_generated target name package generated_dir)
    _pocketjs_python(python)
    set(header "${generated_dir}/pocketjs_app_${name}.h")
    set(source "${generated_dir}/pocketjs_app_${name}.c")
    set(assembly "${generated_dir}/pocketjs_app_${name}.S")
    add_custom_command(
        OUTPUT "${header}" "${source}" "${assembly}"
        COMMAND
            "${python}" "${POCKETJS_IDF_EMBED_TOOL}"
            "--package=${package}"
            "--name=${name}"
            "--output-dir=${generated_dir}"
        DEPENDS
            "${package}"
            "${POCKETJS_IDF_EMBED_TOOL}"
        COMMENT "Validating and embedding PocketJS app '${name}'"
        VERBATIM
    )

    string(MAKE_C_IDENTIFIER "${target}_${name}" generated_target_suffix)
    set(generated_target
        "pocketjs_${generated_target_suffix}_generated"
    )
    add_custom_target(
        "${generated_target}"
        DEPENDS "${header}" "${source}" "${assembly}"
    )
    add_dependencies("${target}" "${generated_target}")
    target_sources("${target}" PRIVATE "${source}" "${assembly}")
    target_include_directories("${target}" PUBLIC "${generated_dir}")
    set_property(
        TARGET "${target}"
        APPEND PROPERTY POCKETJS_EMBEDDED_PACKAGES "${package}"
    )
endfunction()

function(pocketjs_embed_package)
    cmake_parse_arguments(
        APP
        ""
        "TARGET;NAME;PACKAGE"
        ""
        ${ARGN}
    )
    _pocketjs_require_arguments(APP "PACKAGE")
    get_filename_component(package "${APP_PACKAGE}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(NOT EXISTS "${package}")
        message(FATAL_ERROR
            "PocketJS package does not exist: ${package}"
        )
    endif()
    set(generated_dir
        "${CMAKE_CURRENT_BINARY_DIR}/pocketjs/${APP_NAME}"
    )
    _pocketjs_attach_generated(
        "${APP_TARGET}"
        "${APP_NAME}"
        "${package}"
        "${generated_dir}"
    )
endfunction()

function(pocketjs_compile_app)
    cmake_parse_arguments(
        APP
        ""
        "TARGET;NAME;MANIFEST"
        ""
        ${ARGN}
    )
    _pocketjs_require_arguments(APP "MANIFEST")
    get_filename_component(manifest "${APP_MANIFEST}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    if(NOT EXISTS "${manifest}")
        message(FATAL_ERROR
            "PocketJS manifest does not exist: ${manifest}"
        )
    endif()
    find_program(POCKETJS_BUN_EXECUTABLE NAMES bun)
    if(NOT POCKETJS_BUN_EXECUTABLE)
        message(FATAL_ERROR
            "pocketjs_compile_app() requires Bun. Install Bun, then run "
            "'bun install --cwd ${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs "
            "--frozen-lockfile'. Use pocketjs_embed_package() to build "
            "without Bun."
        )
    endif()
    if(NOT EXISTS
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/node_modules"
    )
        message(FATAL_ERROR
            "PocketJS compiler dependencies are missing. Run "
            "'bun install --cwd ${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs "
            "--frozen-lockfile'. Dependencies are never installed "
            "automatically."
        )
    endif()

    set(generated_dir
        "${CMAKE_CURRENT_BINARY_DIR}/pocketjs/${APP_NAME}"
    )
    set(package "${generated_dir}/${APP_NAME}.pocket")
    set(depfile "${generated_dir}/${APP_NAME}.d")
    file(GLOB_RECURSE pocketjs_tool_sources CONFIGURE_DEPENDS
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/*.ts"
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/*.json"
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/*.ttf"
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/*.woff2"
        "${POCKETJS_IDF_COMPONENT_DIR}/vendor/pocketjs/bun.lock"
        "${POCKETJS_IDF_BUILD_TOOL}"
    )
    list(FILTER pocketjs_tool_sources EXCLUDE REGEX
        "/(node_modules|\\.cache|dist|target)/"
    )
    add_custom_command(
        OUTPUT "${package}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${generated_dir}"
        COMMAND
            "${POCKETJS_BUN_EXECUTABLE}" "${POCKETJS_IDF_BUILD_TOOL}"
            "--manifest=${manifest}"
            "--output=${package}"
            "--work-dir=${generated_dir}"
            "--depfile=${depfile}"
        DEPENDS
            "${manifest}"
            ${pocketjs_tool_sources}
        DEPFILE "${depfile}"
        COMMENT "Compiling PocketJS app '${APP_NAME}' for esp32p4-idf ABI 1"
        VERBATIM
    )
    _pocketjs_attach_generated(
        "${APP_TARGET}"
        "${APP_NAME}"
        "${package}"
        "${generated_dir}"
    )
endfunction()
