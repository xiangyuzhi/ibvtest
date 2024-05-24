cmake_minimum_required(VERSION 3.10)
project(perftest-concise)

find_library(IBVERBS_LIBRARY ibverbs HINTS /home/atr/local/lib)
find_library(RDMACM_LIBRARY rdmacm HINTS /home/atr/local/lib)
link_libraries(pthread ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY})
include_directories("${PROJECT_SOURCE_DIR}" "/home/atr/local/include/")

SET( CMAKE_CXX_FLAGS  "-Ofast -lrt -std=c++11 -DHAVE_CXX0X -march=native -fpic -w -fopenmp -ftree-vectorize -ftree-vectorizer-verbose=0" )

add_executable(read utils/get_clock.cc read_latency.cc )




