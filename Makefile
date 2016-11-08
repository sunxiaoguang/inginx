PROJECT_HOME = .
BUILD_DIR ?= build/make
include $(BUILD_DIR)/make.defs

SUBDIRS += src sample

include $(BUILD_DIR)/make.rules
