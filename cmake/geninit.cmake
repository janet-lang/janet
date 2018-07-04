# Include bin2h cmake code
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

include(bin2h)

bin2h (
    SOURCE_FILE ${CMAKE_CURRENT_LIST_DIR}/../src/mainclient/init.dst
    HEADER_FILE "generated/init.h"
    VARIABLE_NAME dst_mainclient_init
)
