include_directories(${PROJECT_SOURCE_DIR}/src)

find_package(Python 3.10 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

pybind11_add_module(pyserf wrapper.cpp)
target_link_libraries(pyserf PRIVATE serf)