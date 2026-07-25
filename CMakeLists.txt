idf_component_register(
    SRCS
        "src/pocketjs.c"
        "src/pocketjs_allocator.c"
        "src/pocketjs_display.c"
        "src/pocketjs_package.c"
        "src/pocketjs_ppa.c"
        "src/pocketjs_runtime.c"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "private_include"
    REQUIRES
        esp_driver_ppa
        freertos
        quickjs-ng
    PRIV_REQUIRES
        esp_timer
        heap
        log
)

if(NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR
        "PocketJS-IDF v0.1 supports only IDF_TARGET=esp32p4."
    )
endif()
if(DEFINED IDF_VERSION AND IDF_VERSION VERSION_LESS "5.4")
    message(FATAL_ERROR
        "PocketJS-IDF requires ESP-IDF 5.4 or newer."
    )
endif()

find_program(POCKETJS_CARGO cargo)
if(NOT POCKETJS_CARGO)
    message(FATAL_ERROR
        "PocketJS-IDF requires Cargo. Install Rust with rustup, then run "
        "'rustup target add riscv32imafc-unknown-none-elf'."
    )
endif()

set(POCKETJS_RUST_TARGET "riscv32imafc-unknown-none-elf")
find_program(POCKETJS_RUSTUP rustup)
if(POCKETJS_RUSTUP)
    execute_process(
        COMMAND "${POCKETJS_RUSTUP}" target list --installed
        OUTPUT_VARIABLE POCKETJS_INSTALLED_RUST_TARGETS
        RESULT_VARIABLE POCKETJS_RUSTUP_RESULT
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(
        NOT POCKETJS_RUSTUP_RESULT EQUAL 0 OR
        NOT POCKETJS_INSTALLED_RUST_TARGETS MATCHES
            "(^|[\r\n])${POCKETJS_RUST_TARGET}([\r\n]|$)"
    )
        message(FATAL_ERROR
            "PocketJS-IDF requires Rust target ${POCKETJS_RUST_TARGET}. "
            "Install it with 'rustup target add ${POCKETJS_RUST_TARGET}'."
        )
    endif()
endif()

set(POCKETJS_RUST_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml")
set(POCKETJS_RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo")
set(POCKETJS_RUST_LIBRARY
    "${POCKETJS_RUST_TARGET_DIR}/${POCKETJS_RUST_TARGET}/release/libpocketjs_esp32_core.a"
)

file(GLOB_RECURSE POCKETJS_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/*.rs"
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/*.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.lock"
)

add_custom_command(
    OUTPUT "${POCKETJS_RUST_LIBRARY}"
    COMMAND
        "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${POCKETJS_RUST_TARGET_DIR}"
        "${POCKETJS_CARGO}" build
        --manifest-path "${POCKETJS_RUST_MANIFEST}"
        --package pocketjs-esp32-core
        --target "${POCKETJS_RUST_TARGET}"
        --release
        --locked
    DEPENDS ${POCKETJS_RUST_SOURCES}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/rust"
    COMMENT "Building PocketJS Core for ESP32-P4 with Rust"
    VERBATIM
)

add_custom_target(
    pocketjs_idf_rust_build
    DEPENDS "${POCKETJS_RUST_LIBRARY}"
)

add_library(pocketjs_idf_rust STATIC IMPORTED GLOBAL)
set_target_properties(pocketjs_idf_rust PROPERTIES
    IMPORTED_LOCATION "${POCKETJS_RUST_LIBRARY}"
)
add_dependencies(pocketjs_idf_rust pocketjs_idf_rust_build)
target_link_libraries(${COMPONENT_LIB} PUBLIC pocketjs_idf_rust)

# Root C bridge symbols referenced by the no_std Rust archive.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,--undefined=pocketjs_rust_alloc"
    "-Wl,--undefined=pocketjs_rust_dealloc"
    "-Wl,--undefined=pocketjs_rust_panic"
    "-Wl,--undefined=pocketjs_ppa_fill_rgb565_bridge"
    "-Wl,--undefined=pocketjs_ppa_blend_a8_rgb565_bridge"
    "-Wl,--undefined=pocketjs_ppa_srm_psm5650_rgb565_bridge"
)
