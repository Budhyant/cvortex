cmake_minimum_required(VERSION 3.0)
project(cvortex)
set(CVORTEX_VERSION_MAJOR 0)
set(CVORTEX_VERSION_MINOR 3)
set(CVORTEX_VERSION_PATCH 8)
set(CVORTEX_VERSION ${CVORTEX_VERSION_MAJOR}.${CVORTEX_VERSION_MINOR}.${CVORTEX_VERSION_PATCH})

option(BUILD_UNIT_TESTS "Builds tests" OFF)
option(BUILD_BENCHMARKS "Builds benchmarks" OFF)
option(USE_OPENMP "Use the OpenMP multithreading" ON)
option(USE_OPENCL "Use OpenCL gpgpu computing" ON)
#option(USE_CPUINFO "Include CPU/platform information lib" ON)
#option(CPUINFO_STATIC_LINKING "Link CPUINFO library statically. Otherwise dynamic." ON)
option(BUILD_STATIC_LIBRARY "Builds static library instead of shared" OFF)

# Everything is placed in the one dictionary. Life is easier.
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
file (GLOB CVORTEX_INCLUDE "include/cvortex/libcvtx.h") # So shoot me for GLOBing.
file (GLOB CVORTEX_SOURCE  "src/*.h" "src/*.cpp")
file (GLOB CVORTEX_OPENCL  "src/*.cl")
source_group("" FILES ${cvortex})
source_group("include" FILES ${CVORTEX_INCLUDE})
source_group("source" FILES ${CVORTEX_SOURCE})
source_group("opencl" FILES ${CVORTEX_OPENCL})

include_directories (include/cvortex)
if(BUILD_STATIC_LIBRARY)
	add_library(cvortex ${CVORTEX_INCLUDE} ${CVORTEX_SOURCE})
	target_compile_definitions(cvortex PRIVATE CVTX_EXPORT=)
else()
	add_library(cvortex SHARED ${CVORTEX_INCLUDE} ${CVORTEX_SOURCE})
	if(WIN32)
		target_compile_definitions(cvortex PRIVATE CVTX_EXPORT=__declspec\(dllexport\))
	else()
		target_compile_definitions(cvortex PRIVATE CVTX_EXPORT=)
	endif()
endif(BUILD_STATIC_LIBRARY)
# We don't want warnings...
target_compile_definitions(cvortex PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_definitions(cvortex PRIVATE  CVORTEX_VERSION=${CVORTEX_VERSION}
											CVORTEX_VERSION_MAJOR=${CVORTEX_VERSION_MAJOR}
											CVORTEX_VERSION_MINOR=${CVORTEX_VERSION_MINOR}
											CVORTEX_VERSION_PATCH=${CVORTEX_VERSION_PATCH})
if(USE_OPENMP)
	find_package(OpenMP)
	if(OpenMP_CXX_FOUND)
		target_link_libraries(cvortex PUBLIC OpenMP::OpenMP_CXX)
		target_compile_definitions(cvortex PRIVATE CVTX_USING_OPENMP)
	endif()
endif(USE_OPENMP)

if(USE_OPENCL)
    find_package(OpenCL REQUIRED)
    target_link_libraries(cvortex PRIVATE ${OpenCL_LIBRARIES})
    target_include_directories(cvortex PRIVATE ${OpenCL_INCLUDE_DIRS})
	target_compile_definitions(cvortex PRIVATE CVTX_USING_OPENCL)
endif(USE_OPENCL)

#if(USE_CPUINFO)
#	find_package(cpuinfo REQUIRED)
#	target_link_libraries(cvortex PRIVATE cpuinfo::clog cpuinfo::cpuinfo)
#	target_compile_definitions(cvortex PRIVATE CVTX_USING_CPUINFO)
#endif(USE_CPUINFO)

find_package(bsv CONFIG REQUIRED)
target_link_libraries(cvortex PUBLIC bsv)
target_include_directories(cvortex PUBLIC bsv)
#set_target_properties(cvortex PROPERTIES LINKER_LANGUAGE CXX)

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    target_link_libraries(cvortex PUBLIC m)   # Maths std library.
endif()
						
set_property(TARGET cvortex PROPERTY FOLDER "libraries")
set_target_properties(cvortex PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
                      PUBLIC_HEADER "${CVORTEX_INCLUDE}")

if(BUILD_UNIT_TESTS)
    add_subdirectory(test)
endif(BUILD_UNIT_TESTS)

if(BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif(BUILD_BENCHMARKS)
