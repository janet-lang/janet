# Include bin2h cmake code
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

include(bin2h)

bin2h (
    SOURCE_FILE ${CMAKE_CURRENT_LIST_DIR}/../src/core/core.dst
    HEADER_FILE "generated/core.h"
    VARIABLE_NAME dst_gen_core
)
