# Include bin2h cmake code
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
include(bin2h)

bin2h (
    SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../src/compiler/boot.dst
    HEADER_FILE "dststlbootstrap.gen.h"
    VARIABLE_NAME dst_stl_bootstrap_gen
)
