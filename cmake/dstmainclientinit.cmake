# Include bin2h cmake codeo
set (CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake")
include(bin2h)

bin2h (
    SOURCE_FILE ${CMAKE_CURRENT_SOURCE_DIR}/../src/mainclient/init.dst
    HEADER_FILE "clientinit.h"
    VARIABLE_NAME "dst_mainclient_init"
)
