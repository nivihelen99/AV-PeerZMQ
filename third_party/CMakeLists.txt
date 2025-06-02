include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3 # Using the same version as in the Makefile
)

FetchContent_MakeAvailable(nlohmann_json)

# nlohmann/json should provide the nlohmann_json::nlohmann_json target.
# We rely on this target being available.
if(NOT TARGET nlohmann_json::nlohmann_json)
    message(FATAL_ERROR "nlohmann_json::nlohmann_json target not found after FetchContent_MakeAvailable. This is unexpected.")
else()
    message(STATUS "Using nlohmann_json::nlohmann_json target from FetchContent.")
endif()
