cmake_minimum_required(VERSION 3.10)
project(perftest-concise)

#find_library(IBVERBS_LIBRARY ibverbs HINTS /usr/lib/x86_64-linux-gnu/libibverbs.so.1)
#find_library(RDMACM_LIBRARY rdmacm HINTS /usr/lib/x86_64-linux-gnu/librdmacm.so.1)
#link_libraries(pthread ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY})
link_libraries(pthread /usr/lib/x86_64-linux-gnu/libibverbs.so.1 /usr/lib/x86_64-linux-gnu/librdmacm.so.1)
#include_directories("${PROJECT_SOURCE_DIR}" "/data/share/project/flavius/usr/include")
include_directories("${PROJECT_SOURCE_DIR}" "/data/share/project/grasper/rdma-core/build/include")

SET(CMAKE_CXX_FLAGS  "-Ofast -lrt -std=c++11 -DHAVE_CXX0X -march=native -fpic -fopenmp -ftree-vectorize -w -ftree-vectorizer-verbose=0")

add_executable(read utils/get_clock.cc read_latency.cc )
add_executable(rw utils/get_clock.cc read_write.cc )



