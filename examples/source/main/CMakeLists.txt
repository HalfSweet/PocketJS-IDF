idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES PocketJS-IDF
)

pocketjs_compile_app(
    TARGET ${COMPONENT_LIB}
    NAME hello
    MANIFEST "app/pocket.json"
)
