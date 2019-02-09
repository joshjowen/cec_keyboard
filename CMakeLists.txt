cmake_minimum_required(VERSION 2.8.9 FATAL_ERROR)
project(cec_keyboard)

set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ON)
add_definitions(-ldl)

find_package(yaml-cpp REQUIRED)

find_package(Boost REQUIRED system)
find_package(websocketpp REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONCPP jsoncpp)

## Print the variables set by the packages above
#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

include_directories(
                    ${YAML_CPP_INCLUDE_DIR}
                    ${WEBSOCKETPP_INCLUDE_DIR}
                    ${Boost_INCLUDE_DIR}
                    ${JSONCPP_INCLUDE_DIRS})
link_directories(
                 ${Boost_LIBRARY_DIRS}
                 ${JSONCPP_LIBRARIES})

add_executable (${PROJECT_NAME}
                inputdevice/inputdevice.cpp
                ${PROJECT_NAME}.cpp)

target_link_libraries(${PROJECT_NAME}
                      ${YAML_CPP_LIBRARIES}
                      ${CMAKE_DL_LIBS}
                      ${Boost_LIBRARIES}
                      ${JSONCPP_LIBRARIES}
                      pthread)

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION /usr/bin)
