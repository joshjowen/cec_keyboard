cmake_minimum_required(VERSION 2.8.9 FATAL_ERROR)
project(cec_keyboard)

set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ON)
add_definitions(-ldl)

find_package(yaml-cpp REQUIRED)

include_directories(${YAML_CPP_INCLUDE_DIRS})
link_directories(${YAML_CPP_LIBRARY_DIRS})
add_definitions(${YAML_CPP_DEFINITIONS})

add_executable (${PROJECT_NAME} inputdevice/inputdevice.cpp ${PROJECT_NAME}.cpp)
target_link_libraries(${PROJECT_NAME} ${YAML_CPP_LIBRARIES} ${CMAKE_DL_LIBS})

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin)
