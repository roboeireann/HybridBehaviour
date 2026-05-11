# Top-level Makefile for HybridBehaviour project

# ── Wide-character Unicode glyphs ──────────────────────────────────────────
# Wide chars are ON by default.  To use ASCII fallbacks ('>' / '<' / 'O') with
# plain ncurses, build with:  make USE_WIDE_CHARS=0
# Requires the ncursesw development library (e.g. libncursesw5-dev on Debian/Ubuntu).
USE_WIDE_CHARS ?= 1

# Compiler and flags
CXX := g++-13
CXXFLAGS := -std=c++20 -Wall -Wextra -g -Iinclude -IsoccerGridSim -IbehaviourExample

ifeq ($(USE_WIDE_CHARS),1)
  CXXFLAGS += -DUSE_WIDE_CHARS
  LDFLAGS  := -lncursesw
else
  LDFLAGS  := -lncurses
endif

# Directories
BUILD_DIR := build
BIN_DIR := bin

# soccerGridSim configuration
SOCCER_BUILD_DIR := $(BUILD_DIR)/soccerGridSim
SOCCER_TARGET := $(BIN_DIR)/soccerGridSim
SOCCER_SRCS := \
	soccerGridSim/main.cpp \
	soccerGridSim/Soccer.cpp \
	soccerGridSim/SimEngine.cpp \
	soccerGridSim/NcursesDisplay.cpp
SOCCER_OBJS := $(patsubst soccerGridSim/%.cpp,$(SOCCER_BUILD_DIR)/%.o,$(SOCCER_SRCS))

# behaviourExample configuration
BEHAVIOUR_BUILD_DIR  := $(BUILD_DIR)/behaviourExample
BEHAVIOUR_TARGET     := $(BIN_DIR)/behaviourExample

# Compilation units for behaviourExample
BEHAVIOUR_SRCS := \
  behaviourExample/main.cpp \
  behaviourExample/HbsExampleTeamPlayers.cpp \
	behaviourExample/Behaviour/BehaviourDefinitions.cpp

# Task behaviour headers – included into HbsExampleTeamPlayers.cpp, not compiled separately.
# Add new Behaviour/*.h files here so that touching them triggers a rebuild.
BEHAVIOUR_TASK_HDRS := \
	behaviourExample/Behaviour/BehaviourDeclarations.h \
  behaviourExample/Behaviour/CoordinateTeam.h \
	behaviourExample/Behaviour/TeamComms.h \
	behaviourExample/Behaviour/BallPlayer.h \
	behaviourExample/Behaviour/Supporter.h \
	behaviourExample/Behaviour/Move.h \
	behaviourExample/Behaviour/FindBall.h

BEHAVIOUR_OBJS := $(patsubst behaviourExample/%.cpp,$(BEHAVIOUR_BUILD_DIR)/%.o,$(BEHAVIOUR_SRCS))

.PHONY: all clean soccerGridSim behaviourExample

all: soccerGridSim behaviourExample

# Create necessary directories
# (Note build directories created in targets where needed)
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# soccerGridSim target
soccerGridSim: $(SOCCER_TARGET)

$(SOCCER_TARGET): $(SOCCER_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SOCCER_BUILD_DIR)/%.o: soccerGridSim/%.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Header dependencies for soccerGridSim
$(SOCCER_BUILD_DIR)/main.o: \
	soccerGridSim/main.cpp \
	soccerGridSim/SimEngine.h \
	soccerGridSim/NcursesDisplay.h \
	soccerGridSim/SimpleChaseTeam.h \
	soccerGridSim/Soccer.h \
	soccerGridSim/Display.h
$(SOCCER_BUILD_DIR)/Soccer.o: \
	soccerGridSim/Soccer.h
$(SOCCER_BUILD_DIR)/SimEngine.o: \
	soccerGridSim/SimEngine.cpp \
	soccerGridSim/SimEngine.h \
	soccerGridSim/Soccer.h \
	soccerGridSim/Display.h
$(SOCCER_BUILD_DIR)/NcursesDisplay.o: \
	soccerGridSim/NcursesDisplay.cpp \
	soccerGridSim/NcursesDisplay.h \
	soccerGridSim/Display.h

# behaviourExample target
behaviourExample: $(BEHAVIOUR_TARGET)

SOCCER_LIB_OBJS := \
  $(SOCCER_BUILD_DIR)/Soccer.o \
  $(SOCCER_BUILD_DIR)/SimEngine.o \
  $(SOCCER_BUILD_DIR)/NcursesDisplay.o

$(BEHAVIOUR_TARGET): $(BEHAVIOUR_OBJS) $(SOCCER_LIB_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BEHAVIOUR_BUILD_DIR)/%.o: behaviourExample/%.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ── Header dependencies for behaviourExample ─────────────────────────────────
#
# List every header whose change should trigger a recompile of that object.
# Because the Behaviour/*.h task files are #included into HbsExampleTeamPlayers.cpp
# (not compiled as separate units), they appear as deps of that object only.
# Adding a new Behaviour/*.h? Add it to BEHAVIOUR_TASK_HDRS above – it is
# referenced here via that variable.

BEHAVIOUR_COMMON_HDRS := \
	soccerGridSim/Soccer.h \
  include/HybridBehaviour.h \
  include/HbsMacroUtils.h \
  include/HbsTypes.h \
  include/TickTrace.h

$(BEHAVIOUR_BUILD_DIR)/main.o: \
  behaviourExample/main.cpp \
  behaviourExample/HbsExampleTeamPlayers.h \
	soccerGridSim/SimpleChaseTeam.h \
  $(BEHAVIOUR_COMMON_HDRS)

$(BEHAVIOUR_BUILD_DIR)/HbsExampleTeamPlayers.o: \
  behaviourExample/HbsExampleTeamPlayers.cpp \
  behaviourExample/HbsExampleTeamPlayers.h \
  $(BEHAVIOUR_COMMON_HDRS)

$(BEHAVIOUR_BUILD_DIR)/Behaviour/BehaviourDefinitions.o: \
  behaviourExample/Behaviour/BehaviourDefinitions.cpp \
  $(BEHAVIOUR_TASK_HDRS) \
  $(BEHAVIOUR_COMMON_HDRS)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
