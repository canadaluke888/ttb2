cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" TTB2_VERSION LIMIT_COUNT 1)

project(ttb2
    VERSION "${TTB2_VERSION}"
    DESCRIPTION "Terminal Table Builder 2.0"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(TTB2_BUILD_TESTS "Build TTB2 test executables" ON)
option(TTB2_ENABLE_PACKAGING "Enable install/package metadata" ON)

include(GNUInstallDirs)
find_package(PkgConfig QUIET)

find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)
endif()

if(PkgConfig_FOUND)
    pkg_check_modules(JSONC QUIET IMPORTED_TARGET json-c)
    pkg_check_modules(NCURSESW QUIET IMPORTED_TARGET ncursesw)
    pkg_check_modules(PANELW QUIET IMPORTED_TARGET panelw)
endif()

if(NOT JSONC_FOUND)
    find_path(JSONC_INCLUDE_DIR json-c/json.h)
    find_library(JSONC_LIBRARY NAMES json-c jsonc)
    if(NOT JSONC_INCLUDE_DIR OR NOT JSONC_LIBRARY)
        message(FATAL_ERROR "json-c was not found. Install json-c or provide JSONC_INCLUDE_DIR/JSONC_LIBRARY.")
    endif()
endif()

if(NOT NCURSESW_FOUND)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
endif()

if(NOT PANELW_FOUND)
    find_package(CursesPanel REQUIRED)
endif()

file(GLOB_RECURSE TTB2_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
list(FILTER TTB2_SOURCES EXCLUDE REGEX "/src/main\\.c$")

add_library(ttb2_deps INTERFACE)
target_include_directories(ttb2_deps
    INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)

add_library(ttb2_core STATIC ${TTB2_SOURCES})
target_include_directories(ttb2_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)
target_compile_options(ttb2_core PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra>
)

if(WIN32)
    target_compile_definitions(ttb2_deps INTERFACE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        strcasecmp=_stricmp
        strncasecmp=_strnicmp
    )
endif()

if(TARGET SQLite::SQLite3)
    target_link_libraries(ttb2_deps INTERFACE SQLite::SQLite3)
elseif(TARGET PkgConfig::SQLITE3)
    target_link_libraries(ttb2_deps INTERFACE PkgConfig::SQLITE3)
endif()

if(TARGET PkgConfig::JSONC)
    target_link_libraries(ttb2_deps INTERFACE PkgConfig::JSONC)
else()
    target_include_directories(ttb2_deps INTERFACE "${JSONC_INCLUDE_DIR}")
    target_link_libraries(ttb2_deps INTERFACE "${JSONC_LIBRARY}")
endif()

if(TARGET PkgConfig::NCURSESW)
    target_link_libraries(ttb2_deps INTERFACE PkgConfig::NCURSESW)
else()
    target_include_directories(ttb2_deps INTERFACE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(ttb2_deps INTERFACE ${CURSES_LIBRARIES})
endif()

if(TARGET PkgConfig::PANELW)
    target_link_libraries(ttb2_deps INTERFACE PkgConfig::PANELW)
else()
    target_link_libraries(ttb2_deps INTERFACE Curses::Panel)
endif()

target_link_libraries(ttb2_deps INTERFACE
    Threads::Threads
    ZLIB::ZLIB
)

if(NOT WIN32)
    target_link_libraries(ttb2_deps INTERFACE m)
endif()

if(WIN32)
    target_link_libraries(ttb2_deps INTERFACE ws2_32)
endif()

target_link_libraries(ttb2_core PUBLIC ttb2_deps)

add_executable(ttb2 src/main.c)
target_link_libraries(ttb2 PRIVATE ttb2_core)

if(TTB2_BUILD_TESTS)
    enable_testing()

    add_executable(history_test
        tests/ui_history_test.c
        src/table.c
        src/table_ops.c
        src/ui/ui_data/ui_history.c
    )
    target_include_directories(history_test PRIVATE include src/ui)
    target_link_libraries(history_test PRIVATE ttb2_deps)
    add_test(NAME history_test COMMAND history_test)

    add_executable(table_index_test
        tests/table_index_test.c
        src/vector/table_index.c
        src/table.c
        src/table_ops.c
        src/db/book_db.c
        src/io/ttb_io.c
    )
    target_include_directories(table_index_test PRIVATE include src/ui)
    target_link_libraries(table_index_test PRIVATE ttb2_deps)
    add_test(NAME table_index_test COMMAND table_index_test)

    add_executable(bookdb_semantic_test
        tests/bookdb_semantic_test.c
        src/vector/table_index.c
        src/table.c
        src/table_ops.c
        src/db/book_db.c
        src/io/ttb_io.c
    )
    target_include_directories(bookdb_semantic_test PRIVATE include src/ui)
    target_link_libraries(bookdb_semantic_test PRIVATE ttb2_deps)
    add_test(NAME bookdb_semantic_test COMMAND bookdb_semantic_test)

    add_executable(settings_test tests/settings_test.c src/settings.c)
    target_include_directories(settings_test PRIVATE include src/ui)
    target_link_libraries(settings_test PRIVATE ttb2_deps)
    add_test(NAME settings_test COMMAND settings_test)

    add_executable(ui_dialog_list_test
        tests/ui_dialog_list_test.c
        src/ui/ui_dialog/ui_dialog_common.c
    )
    target_include_directories(ui_dialog_list_test PRIVATE include src/ui)
    target_link_libraries(ui_dialog_list_test PRIVATE ttb2_deps)
    add_test(NAME ui_dialog_list_test COMMAND ui_dialog_list_test)
endif()

install(TARGETS ttb2 RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(DIRECTORY assets/icons/ DESTINATION "${CMAKE_INSTALL_DATADIR}/pixmaps/ttb2")

if(TTB2_ENABLE_PACKAGING)
    set(CPACK_PACKAGE_NAME "ttb2")
    set(CPACK_PACKAGE_VENDOR "Luke Canada")
    set(CPACK_PACKAGE_CONTACT "Luke Canada <canadaluke888@gmail.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Terminal Table Builder 2.0")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "TGZ")
    else()
        set(CPACK_GENERATOR "TGZ;DEB")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libncursesw6, libsqlite3-0, libjson-c5, zlib1g")
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    endif()
    include(CPack)
endif()
