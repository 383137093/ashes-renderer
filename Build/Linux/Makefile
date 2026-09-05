CXX := g++
INCLUDES := -I../../Source
SRC_DIR := ../../Source

BUILD ?= debug

ifeq ($(BUILD),release)
    BUILD_DIR := obj/release
    TARGET := ../../AshesRenderer.bin
    CXXFLAGS := -Wall -Wextra -O3 -DNDEBUG -flto
    LDFLAGS := -lX11 -flto -O3
else ifeq ($(BUILD),debug)
    BUILD_DIR := obj/debug
    TARGET := ../../AshesRenderer.bin
    CXXFLAGS := -Wall -Wextra -g -O0
    LDFLAGS := -lX11
else
    $(error Unknown BUILD "$(BUILD)", expected "debug" or "release")
endif

CXXFLAGS += -std=c++17 -MMD -MP

SRCS := $(wildcard $(SRC_DIR)/Asset/*.cpp) \
        $(wildcard $(SRC_DIR)/Geometry/*.cpp) \
        $(wildcard $(SRC_DIR)/Math/*.cpp) \
        $(wildcard $(SRC_DIR)/Parser/*.cpp) \
        $(SRC_DIR)/Platform/GenericWindow_Linux.cpp \
        $(wildcard $(SRC_DIR)/Rasterize/*.cpp) \
        $(SRC_DIR)/Main.cpp

OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

.PHONY: all debug release clean relink

all: $(TARGET)

# Recursive re-invocation so CXXFLAGS/TARGET reflect the requested BUILD value.
debug:
	$(MAKE) BUILD=debug all

release:
	$(MAKE) BUILD=release all

# Debug/Release share $(TARGET), so relink is forced every time (via this phony
# prerequisite) to make sure it always matches the requested BUILD, not just
# whichever variant was linked into it last.
relink:

$(TARGET): $(OBJS) relink
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(DEPS)

clean:
	rm -rf obj ../../AshesRenderer.bin
