cmake_minimum_required(VERSION 3.21)
project(sst_consumer LANGUAGES CXX)

add_subdirectory(../.. static_search_tree)
add_executable(consumer ../../examples/simple_search.cpp)
target_link_libraries(consumer PRIVATE sst::static_search_tree)

enable_testing()
add_test(NAME consumer COMMAND consumer)
