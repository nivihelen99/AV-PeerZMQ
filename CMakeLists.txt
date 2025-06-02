cmake_minimum_required(VERSION 3.15)
project(MeshNetwork LANGUAGES CXX)

# Prepend this at the top of the root CMakeLists.txt, after project()
# Set default CXX flags for Debug builds
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
# Set default CXX flags for Release builds (NDEBUG is often default, but good to be explicit)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
# For RelWithDebInfo (Release with Debug Info)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2 -g -DNDEBUG")

# The existing add_compile_options(-Wall -pthread) should remain as they apply to all build types.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# Common compiler flags
add_compile_options(-Wall -pthread)

# Enable testing
enable_testing()

# Add third_party directory
add_subdirectory(third_party)

# Message for build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Setting build type to 'Release' as none was specified.")
endif()

message(STATUS "Project Name: ${PROJECT_NAME}")
message(STATUS "CXX Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}") # Display the build type

# Library target for mesh_network
add_library(mesh_network_lib mesh_network.cpp mesh_network.h)
target_compile_definitions(mesh_network_lib PUBLIC CPPZMQ_HAS_DRAFT_API)

# Link nlohmann/json to the library
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(mesh_network_lib PUBLIC nlohmann_json::nlohmann_json)
else()
    message(FATAL_ERROR "nlohmann_json::nlohmann_json target not found. Check third_party/CMakeLists.txt.")
endif()

# Public include directories for the library, so consumers can find mesh_network.h
target_include_directories(mesh_network_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Find ZeroMQ using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq) # Look for libzmq, prefix variables with ZMQ_

if(ZMQ_FOUND)
    message(STATUS "Configured ZeroMQ using pkg-config variables (Include: ${ZMQ_INCLUDE_DIRS}, Libs: ${ZMQ_LIBRARIES})")
else()
    message(FATAL_ERROR "ZeroMQ (libzmq) not found by pkg-config. Make sure libzmq-dev is installed.")
endif()

# Executable target for example_app
add_executable(example_app example_app.cpp)

# Link mesh_network_lib and ZeroMQ
target_link_libraries(example_app PRIVATE mesh_network_lib)

# Link ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(example_app PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(example_app PRIVATE ${ZMQ_LIBRARIES})
else()
    message(FATAL_ERROR "ZeroMQ (libzmq) not found by pkg-config for target example_app. Ensure libzmq-dev is installed.")
endif()

# Add pthread for linking, as it was in LDFLAGS/LDLIBS
# CMake often handles pthread automatically with FindThreads, but explicit is fine
find_package(Threads REQUIRED)
target_link_libraries(example_app PRIVATE Threads::Threads)

# Executable target for test_mesh_network
add_executable(test_mesh_network test_mesh_network.cpp)

# Link mesh_network_lib and ZeroMQ
target_link_libraries(test_mesh_network PRIVATE mesh_network_lib)

# Link ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(test_mesh_network PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(test_mesh_network PRIVATE ${ZMQ_LIBRARIES})
else()
    message(FATAL_ERROR "ZeroMQ (libzmq) not found by pkg-config for target test_mesh_network. Ensure libzmq-dev is installed.")
endif()

# Link pthreads
target_link_libraries(test_mesh_network PRIVATE Threads::Threads)

# Add to CTest
add_test(NAME MeshNetworkTest COMMAND test_mesh_network)

# Executable target for enhanced_test_suite
add_executable(enhanced_tests enhanced_test_suite.cpp)

# Link mesh_network_lib and ZeroMQ
target_link_libraries(enhanced_tests PRIVATE mesh_network_lib)

# Link ZeroMQ
if(ZMQ_FOUND)
    target_include_directories(enhanced_tests PRIVATE ${ZMQ_INCLUDE_DIRS})
    target_link_libraries(enhanced_tests PRIVATE ${ZMQ_LIBRARIES})
else()
    message(FATAL_ERROR "ZeroMQ (libzmq) not found by pkg-config for target enhanced_tests. Ensure libzmq-dev is installed.")
endif()

# Link pthreads
target_link_libraries(enhanced_tests PRIVATE Threads::Threads)

# Add to CTest
add_test(NAME EnhancedTestSuite COMMAND enhanced_tests)
