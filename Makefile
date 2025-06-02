# Makefile for AV-PeerZMQ Project

# Variables
# -----------------------------------------------------------------------------
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

# Attempt to use pkg-config for ZeroMQ and JsonCpp
PKG_CONFIG_ZEROMQ_CFLAGS := $(shell $(PKG_CONFIG) --cflags libzmq 2>/dev/null || $(PKG_CONFIG) --cflags zeromq 2>/dev/null)
PKG_CONFIG_JSONCPP_CFLAGS := $(shell $(PKG_CONFIG) --cflags jsoncpp 2>/dev/null)
PKG_CONFIG_ZEROMQ_LIBS := $(shell $(PKG_CONFIG) --libs libzmq 2>/dev/null || $(PKG_CONFIG) --libs zeromq 2>/dev/null)
PKG_CONFIG_JSONCPP_LIBS := $(shell $(PKG_CONFIG) --libs jsoncpp 2>/dev/null)

# Initialize CXXFLAGS and LDFLAGS/LDLIBS
CXXFLAGS := $(BASE_CXXFLAGS)
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

# If pkg-config provided CFLAGS for JsonCpp, add them
ifeq ($(strip $(PKG_CONFIG_JSONCPP_CFLAGS)),)
    $(info Warning: pkg-config could not find JsonCpp cflags. Using default include path.)
    CXXFLAGS += $(INC_PATHS) # INC_PATHS might already be added, but no harm if it's the same
else
    CXXFLAGS += $(PKG_CONFIG_JSONCPP_CFLAGS)
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

# If pkg-config provided LIBS for JsonCpp, add them
ifeq ($(strip $(PKG_CONFIG_JSONCPP_LIBS)),)
    $(info Warning: pkg-config could not find JsonCpp libs. Using default lib path and lib name.)
    LDFLAGS += $(LIB_PATHS) # LIB_PATHS might already be added
    LDLIBS += -ljsoncpp
else
    LDFLAGS += $(filter -L%, $(PKG_CONFIG_JSONCPP_LIBS))
    LDLIBS += $(filter-out -L%, $(PKG_CONFIG_JSONCPP_LIBS))
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
$(APP_TARGET): $(APP_OBJS) $(LIB_OBJS)
	@echo "Linking target: $@"
	$(CXX) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# Implicit rule for .cpp to .o compilation is usually sufficient:
# $(CXX) $(CXXFLAGS) -c $< -o $@
# However, explicitly stating it can sometimes be clearer or needed for specific dependencies.
# For this project, an explicit rule for all .o files:
%.o: %.cpp
	@echo "Compiling: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean target
clean:
	@echo "Cleaning project..."
	$(RM) $(APP_TARGET) $(LIB_OBJS) $(APP_OBJS)
	@echo "Clean complete."

# Informative echo (optional)
# $(info Using CXXFLAGS: $(CXXFLAGS))
# $(info Using LDFLAGS: $(LDFLAGS))
# $(info Using LDLIBS: $(LDLIBS))
# $(info LIB_OBJS: $(LIB_OBJS))
# $(info APP_OBJS: $(APP_OBJS))
