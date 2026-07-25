idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
)

pocketjs_embed_package(
    TARGET ${COMPONENT_LIB}
    NAME hello
    PACKAGE "app/hello.pocket"
)
