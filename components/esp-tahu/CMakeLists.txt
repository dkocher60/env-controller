idf_component_register(
    SRC_DIRS "." "tahu/c/core/src"
    INCLUDE_DIRS "." "tahu/c/core/include"
    PRIV_REQUIRES mqtt-app
)

set_source_files_properties("tahu/c/core/src/tahu.c" PROPERTIES 
    COMPILE_OPTIONS "-Wno-error=format")
