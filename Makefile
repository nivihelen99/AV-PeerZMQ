# Makefile for AV-PeerZMQ Project

# Variables
# -----------------------------------------------------------------------------
NLOHMANN_JSON_HPP := third_party/nlohmann/json.hpp
CXX ?= g++
RM ?= rm -f
PKG_CONFIG ?= pkg-config

APP_TARGET := example_app
LIB_SRCS := mesh_network.cpp
APP_SRCS := example_app.cpp

# Generate object file names
LIB_OBJS := $(LIB_SRCS:.cpp=.o)
APP_OBJS := $(APP_SRCS:.cpp=.o)

# Compiler and Linker Flags
# -----------------------------------------------------------------------------
# Base flags
BASE_CXXFLAGS := -std=c++17 -Wall -pthread # Changed to C++17
BASE_LDFLAGS := -pthread
BASE_LDLIBS := -pthread # -pthread can also be needed for linking with g++

# Attempt to use pkg-config for ZeroMQ
PKG_CONFIG_ZEROMQ_CFLAGS := $(shell $(PKG_CONFIG) --cflags libzmq 2>/dev/null || $(PKG_CONFIG) --cflags zeromq 2>/dev/null)
PKG_CONFIG_ZEROMQ_LIBS := $(shell $(PKG_CONFIG) --libs libzmq 2>/dev/null || $(PKG_CONFIG) --libs zeromq 2>/dev/null)

# Initialize CXXFLAGS and LDFLAGS/LDLIBS
CXXFLAGS := $(BASE_CXXFLAGS) -Ithird_party # Add include for nlohmann/json.hpp
LDFLAGS := $(BASE_LDFLAGS)
LDLIBS := $(BASE_LDLIBS) # Start with base, add more below

# If pkg-config provided CFLAGS for ZeroMQ, add them
ifeq ($(strip $(PKG_CONFIG_ZEROMQ_CFLAGS)),)
    $(info Warning: pkg-config could not find ZeroMQ cflags. Using default include path.)
    # INC_PATHS variable can be used for manual override if needed, e.g., make INC_PATHS="-I/opt/custom/include"
    CXXFLAGS += $(INC_PATHS) -I/usr/local/include
else
    CXXFLAGS += $(PKG_CONFIG_ZEROMQ_CFLAGS)
endif

# If pkg-config provided LIBS for ZeroMQ, add them to LDFLAGS (for paths) and LDLIBS (for -l flags)
ifeq ($(strip $(PKG_CONFIG_ZEROMQ_LIBS)),)
    $(info Warning: pkg-config could not find ZeroMQ libs. Using default lib path and lib name.)
    # LIB_PATHS variable can be used for manual override, e.g., make LIB_PATHS="-L/opt/custom/lib"
    LDFLAGS += $(LIB_PATHS) -L/usr/local/lib
    LDLIBS += -lzmq
else
    LDFLAGS += $(filter -L%, $(PKG_CONFIG_ZEROMQ_LIBS)) # Extract -L flags
    LDLIBS += $(filter-out -L%, $(PKG_CONFIG_ZEROMQ_LIBS)) # Extract other flags like -lzmq
endif

# Ensure no duplicate flags if INC_PATHS or LIB_PATHS were used and pkg-config also succeeded
# (This is a simple approach; more complex Makefiles might use $(sort ...))
# CXXFLAGS := $(sort $(CXXFLAGS)) # Requires GNU Make sort function
# LDFLAGS := $(sort $(LDFLAGS))

# Targets
# -----------------------------------------------------------------------------
.PHONY: all clean

all: $(APP_TARGET)

# Rule to link the application
$(APP_TARGET): $(APP_OBJS) $(LIB_OBJS) $(NLOHMANN_JSON_HPP)
	@echo "Linking target: $@"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Explicit rule for .cpp to .o compilation:
# This ensures that NLOHMANN_JSON_HPP is downloaded before compiling any .cpp file.
# If only specific .o files depend on it, you can make the rule more specific.
%.o: %.cpp $(NLOHMANN_JSON_HPP)
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to download nlohmann/json.hpp
$(NLOHMANN_JSON_HPP):
	@echo "Downloading nlohmann/json.hpp..."
	@mkdir -p $(dir $(NLOHMANN_JSON_HPP))
	@curl -L https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp -o $(NLOHMANN_JSON_HPP)
	@echo "Downloaded nlohmann/json.hpp."

# Clean target
clean:
	@echo "Cleaning project..."
	$(RM) $(APP_TARGET) $(LIB_OBJS) $(APP_OBJS)
	# Optionally remove the downloaded header
	# $(RM) $(NLOHMANN_JSON_HPP)
	@echo "Clean complete."

# Informative echo (optional)
# $(info Using CXXFLAGS: $(CXXFLAGS))
# $(info Using LDFLAGS: $(LDFLAGS))
# $(info Using LDLIBS: $(LDLIBS))
# $(info LIB_OBJS: $(LIB_OBJS))
# $(info APP_OBJS: $(APP_OBJS))
