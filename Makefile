# Project Name
TARGET = nuut

USE_DAISYSP_LGPL = 1

# Sources
CPP_SOURCES = nuut.cpp $(wildcard touch/*.cpp) $(wildcard nuut/*.cpp)

# Library Locations
LIBDAISY_DIR = lib/libDaisy/
DAISYSP_DIR = lib/DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core

include $(SYSTEM_FILES_DIR)/Makefile

CPP_STANDARD = -std=gnu++17