PROJECT_HOME = ..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CSRCS += inginx.c
OBJS += $(addprefix $(OUTDIR)/,$(CSRCS:.c=$(OBJ_SUFFIX)))

EXETARGET = inginx

INCLUDE_DIRS += ../include

ifeq ($(THE_OS), linux)
	DEPLIBS += rt dl
endif

DEPLIBS += inginx

include $(BUILD_DIR)/make.rules

$(BINDIR)/inginx$(EXE_SUFFIX) : $(OBJS)

