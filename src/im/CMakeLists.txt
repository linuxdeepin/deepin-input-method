include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(../core)

set(ENGINE dinput)

pkg_check_modules(GLib REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(PY REQUIRED IMPORTED_TARGET libpinyin)

file(GLOB SRCS LIST_DIRECTORIES false *.c *.cpp)
list(APPEND SRCS ../core/msg_queue.c)

add_executable(${ENGINE} ${SRCS})
target_link_libraries(${ENGINE} PUBLIC PkgConfig::GLib rt pthread PkgConfig::PY)

# testing client
add_executable(cli ${SRCS})
target_link_libraries(cli PUBLIC PkgConfig::GLib rt pthread PkgConfig::PY)

#set(UI dinput-panel)

#find_package(Qt5Widgets)
#find_package(Qt5LinguistTools)

#add_executable(${UI} ui.cpp)
#target_link_libraries(${UI} PUBLIC Qt5::Widgets)
