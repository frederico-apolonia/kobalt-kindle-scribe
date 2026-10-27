cmake_minimum_required(VERSION 3.22)
project(KobaltKindle LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Network Sensors Sql)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
    message(FATAL_ERROR "Use the prepared Kindle SDK and the kindle preset (32-bit ARM Linux)")
endif()
get_filename_component(KOBALT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
foreach(retired KOBALT_DEPENDENCIES KOBALT_DEPENDENCY_CACHE KOBALT_ASSETS)
    unset(${retired} CACHE)
endforeach()
set(source "${KOBALT_ROOT}/src")
# Selects the libpng source build in dependencies/: the Kindle SDK has no libpng to share with Qt.
set(KOBALT_KINDLE ON)
add_subdirectory("${KOBALT_ROOT}/dependencies" "${CMAKE_CURRENT_BINARY_DIR}/dependencies")
add_subdirectory("${KOBALT_ROOT}/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets")

file(READ "${KOBALT_ASSETS}/manifest.json" assets)
string(JSON KOBALT_VERSION GET "${assets}" version)
string(JSON count LENGTH "${assets}" files)
math(EXPR last "${count} - 1")
set(bundled)
foreach(index RANGE ${last})
    string(JSON relative GET "${assets}" files ${index} path)
    if(IS_ABSOLUTE "${relative}" OR relative MATCHES "(^|/)\\.\\.(/|$)")
        message(FATAL_ERROR "Invalid runtime asset path: ${relative}")
    endif()
    string(JSON expected GET "${assets}" files ${index} sha256)
    file(SHA256 "${KOBALT_ASSETS}/${relative}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Prepared runtime asset changed: ${relative}")
    endif()
    list(APPEND bundled "${relative}")
endforeach()

file(READ "${CRENGINE_SOURCE}/cr3gui/data/html5.css" READER_HTML_CSS)
configure_file("${source}/engine/crengine/reader_styles.h.in" reader_styles.h @ONLY)
file(GLOB patterns RELATIVE "${CRENGINE_SOURCE}/cr3gui/data/hyph" "${CRENGINE_SOURCE}/cr3gui/data/hyph/*.pattern")
list(SORT patterns)
list(TRANSFORM patterns PREPEND "\"")
list(TRANSFORM patterns APPEND "\"")
list(JOIN patterns ", " READER_HYPHENATION_PATTERNS)
configure_file("${source}/engine/crengine/reader_hyphenation.h.in" reader_hyphenation.h @ONLY)
set(KOBALT_COMMIT "" CACHE STRING "Source commit, passed by the container launcher")
option(KOBALT_COMMIT_MODIFIED "Source has uncommitted changes" OFF)
configure_file("${source}/support/build_info.h.in" build_info.h @ONLY)

# Shared reader code. The Nickel loopback and the system font catalogue have no Kindle counterpart: the loopback
# entry point is stubbed in display.cpp and the Amazon fonts are obfuscated, so only bundled fonts are used.
file(GLOB_RECURSE shared CONFIGURE_DEPENDS "${source}/*.cpp" "${source}/*.h")
list(REMOVE_ITEM shared "${source}/publication/loopback.cpp" "${source}/assets/system_fonts.cpp")
file(GLOB adapters CONFIGURE_DEPENDS "*.cpp" "*.h")
# The reader controller and start screen are portable; the desktop target compiles them from the same place.
list(APPEND adapters "${KOBALT_ROOT}/platforms/android/reader.cpp" "${KOBALT_ROOT}/platforms/android/start_screen.cpp")
qt_add_executable(kobalt_kindle ${shared} ${adapters})
option(KOBALT_SQLITE_ANNOTATIONS_DEFAULT "Use SQLite annotation storage by default" ON)
if(KOBALT_SQLITE_ANNOTATIONS_DEFAULT)
    set_source_files_properties("${source}/storage/annotation_database.cpp" PROPERTIES
        COMPILE_DEFINITIONS KOBALT_SQLITE_ANNOTATIONS_DEFAULT=1)
else()
    set_source_files_properties("${source}/storage/annotation_database.cpp" PROPERTIES
        COMPILE_DEFINITIONS KOBALT_SQLITE_ANNOTATIONS_DEFAULT=0)
endif()
target_include_directories(kobalt_kindle PRIVATE "${CMAKE_CURRENT_BINARY_DIR}" "${source}/interfaces/reader/ui/controls"
    "${KOBALT_ROOT}/platforms/android")
target_include_directories(kobalt_kindle SYSTEM PRIVATE "${CRENGINE_SOURCE}/crengine/include" "${CRENGINE_BUILD}"
    "${prefix}/include" "${prefix}/include/harfbuzz" "${prefix}/include/freetype2" "${prefix}/include/fribidi")
target_compile_options(kobalt_kindle PRIVATE -Wall -Wextra "-iquote${KOBALT_ROOT}" "-iquote${source}")
target_compile_definitions(kobalt_kindle PRIVATE KOBALT_READER_HARDWARE KOBALT_VERSION="${KOBALT_VERSION}")
include("${KOBALT_ROOT}/platforms/cache-policy.cmake")
target_compile_definitions(kobalt_kindle PRIVATE KOBALT_LAYOUT_POLICY="${layout_policy}")
# Qt bundles its own FreeType. The engine's glyph calls are redirected to the wrappers in font_weight.cpp so the two
# copies stay apart, as on Android.
set(engine "${CMAKE_CURRENT_BINARY_DIR}/libcrengine-reader.a")
add_custom_command(OUTPUT "${engine}"
    COMMAND "${CMAKE_OBJCOPY}" --redefine-sym FT_Load_Glyph=kobalt_font_load_glyph
        --redefine-sym FT_Render_Glyph=kobalt_font_render_glyph "${CRENGINE_BUILD}/libcrengine.a" "${engine}"
    DEPENDS "${CRENGINE_BUILD}/libcrengine.a" VERBATIM)
add_custom_target(reader-engine DEPENDS "${engine}")
add_dependencies(reader-engine kobalt-dependencies)
add_dependencies(kobalt_kindle reader-engine)
target_link_libraries(kobalt_kindle PRIVATE Qt6::Widgets Qt6::Network Qt6::Sensors Qt6::Sql "${engine}"
    "${prefix}/lib/libharfbuzz.a" "${prefix}/lib/libxxhash.a" "${prefix}/lib/libutf8proc.a"
    "${prefix}/lib/libjpeg.a" "${prefix}/lib/liblunasvg.a" "${prefix}/lib/libfreetype.a"
    "${prefix}/lib/libfribidi.a" "${prefix}/lib/libunibreak.a" "${prefix}/lib/libpng16.a" "${prefix}/lib/libz.a")
target_link_options(kobalt_kindle PRIVATE -Wl,--exclude-libs,ALL)
set_target_properties(kobalt_kindle PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
set(resources "${KOBALT_ROOT}/assets")
file(GLOB notices CONFIGURE_DEPENDS "${resources}/notices/*")
qt_add_resources(kobalt_kindle notices PREFIX "/kobalt" BASE "${resources}" FILES "${resources}/acknowledgments.json" ${notices})

# Runtime layout next to the executable: kobalt, assets/ (verified above) and the plugin come from this build; lib/
# (Qt and the C++ runtime) and plugins/sqldrivers come from the Qt SDK.
set(package "${CMAKE_CURRENT_BINARY_DIR}/package")
set(staged)
foreach(relative IN LISTS bundled)
    add_custom_command(OUTPUT "${package}/assets/${relative}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${KOBALT_ASSETS}/${relative}" "${package}/assets/${relative}"
        DEPENDS "${KOBALT_ASSETS}/${relative}" VERBATIM)
    list(APPEND staged "${package}/assets/${relative}")
endforeach()
add_custom_command(OUTPUT "${package}/assets/manifest.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${KOBALT_ASSETS}/manifest.json" "${package}/assets/manifest.json"
    DEPENDS "${KOBALT_ASSETS}/manifest.json" VERBATIM)
list(APPEND staged "${package}/assets/manifest.json")
add_custom_command(OUTPUT "${package}/kobalt"
    COMMAND "${CMAKE_STRIP}" -o "${package}/kobalt" "$<TARGET_FILE:kobalt_kindle>"
    DEPENDS kobalt_kindle VERBATIM)
# The display and input plugin, the framebuffer guard and the launcher scripts.
set(FBINK_SOURCE "$ENV{FBINK_SOURCE}" CACHE PATH "FBInk source tree with Release/libfbink.a built for the Kindle")
add_subdirectory(display)
add_custom_command(OUTPUT "${package}/plugins/platforms/libqkindlefb.so"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${package}/plugins/platforms"
    COMMAND "${CMAKE_STRIP}" -o "${package}/plugins/platforms/libqkindlefb.so" "$<TARGET_FILE:qkindlefb>"
    DEPENDS qkindlefb VERBATIM)
add_custom_command(OUTPUT "${package}/fbguard"
    COMMAND "${CMAKE_STRIP}" -o "${package}/fbguard" "$<TARGET_FILE:fbguard>"
    DEPENDS fbguard VERBATIM)
foreach(script kobalt.sh library-entry.sh)
    add_custom_command(OUTPUT "${package}/${script}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/package/${script}" "${package}/${script}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/package/${script}" VERBATIM)
    list(APPEND staged "${package}/${script}")
endforeach()
add_custom_target(package DEPENDS "${package}/kobalt" "${package}/plugins/platforms/libqkindlefb.so"
    "${package}/fbguard" ${staged})
