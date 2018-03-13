# Include bin2h cmake codeo
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
include(bin2h)

bin2h (
    SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../src/compiler/boot.dst
    HEADER_FILE "dststlbootstrap.h"
    VARIABLE_NAME dst_stl_bootstrap_gen
)
