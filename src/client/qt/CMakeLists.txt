set(DIM libdeepinplatforminputcontextplugin)
set(CMAKE_AUTOMOC on)
include_directories(${CMAKE_CURRENT_BINARY_DIR})
# qt5 im
find_package(Qt5Gui COMPONENTS Private)
add_library(${DIM} SHARED dim_input_context.cpp)
target_include_directories(${DIM} PRIVATE ${Qt5Gui_PRIVATE_INCLUDE_DIRS} ${CMAKE_BINARY_DIR})
target_link_libraries(${DIM} Qt5::Gui)

