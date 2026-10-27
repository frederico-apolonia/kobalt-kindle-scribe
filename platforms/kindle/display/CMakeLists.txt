# The Kindle display and input plugin (QPA platform "kindlefb") and fbguard, the framebuffer snapshot tool the
# launcher uses to give the Amazon UI its screen back. Both link FBInk, which is GPLv3 and is not vendored: build its
# static library for the Kindle first and point FBINK_SOURCE at that tree (see doc/kindle.md).
find_package(Qt6 REQUIRED COMPONENTS CorePrivate GuiPrivate)
if(NOT EXISTS "${FBINK_SOURCE}/fbink.h" OR NOT EXISTS "${FBINK_SOURCE}/Release/libfbink.a")
    message(FATAL_ERROR "Build FBInk for the Kindle first and set FBINK_SOURCE (found '${FBINK_SOURCE}')")
endif()
add_library(qkindlefb MODULE plugin.cpp kindlefb.cpp kindlefb.h kindleinput.cpp kindleinput.h kindleinput_core.h
    kindlelifecycle.cpp kindlelifecycle.h)
target_link_libraries(qkindlefb PRIVATE Qt6::CorePrivate Qt6::GuiPrivate "${FBINK_SOURCE}/Release/libfbink.a")
target_compile_options(qkindlefb PRIVATE -idirafter${FBINK_SOURCE} -Wall -Wextra -Werror)
set_target_properties(qkindlefb PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/platforms"
    BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/../../lib")
add_executable(fbguard fbguard.c)
target_compile_options(fbguard PRIVATE -idirafter${FBINK_SOURCE} -Wall -Wextra -Werror)
target_link_libraries(fbguard PRIVATE "${FBINK_SOURCE}/Release/libfbink.a")
