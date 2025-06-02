include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Using the same version as in the Makefile
)

FetchContent_MakeAvailable(nlohmann_json)

# Create an interface library for nlohmann_json to easily link against it.
# This is good practice as nlohmann/json is a header-only library.
add_library(nlohmann_json_interface INTERFACE)
target_include_directories(nlohmann_json_interface INTERFACE
  ${nlohmann_json_SOURCE_DIR}/include
)
# If nlohmann_json itself creates a target (e.g., nlohmann_json::nlohmann_json for v3.10+),
# it's often better to use that directly. FetchContent_MakeAvailable should make such targets available.
# For older versions or if it doesn't, an interface library is a good approach.
# Checking the nlohmann/json documentation, it provides `nlohmann_json::nlohmann_json` target.
# So we can rely on that if FetchContent_MakeAvailable makes it available.
# For robustness, we can check if the target exists.

if(NOT TARGET nlohmann_json::nlohmann_json)
    message(STATUS "nlohmann_json::nlohmann_json target not found after FetchContent_MakeAvailable. Using custom interface library.")
    # Fallback to our custom interface if the official one isn't created by FetchContent_MakeAvailable
    # This part is mostly for older versions or if something goes wrong with FetchContent's target creation.
    # Modern nlohmann/json with CMake should create nlohmann_json::nlohmann_json.
else()
    message(STATUS "Using nlohmann_json::nlohmann_json target from FetchContent.")
    # It's good practice to alias it if you want a consistent name, but not strictly necessary.
    add_library(nlohmann_json_alias ALIAS nlohmann_json::nlohmann_json)
endif()
