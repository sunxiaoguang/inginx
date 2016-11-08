PROJECT_HOME = ..
BUILD_DIR ?= $(PROJECT_HOME)/build/make

include $(BUILD_DIR)/make.defs

CSRCS += adlist.c ae.c anet.c networking.c sds.c server.c zmalloc.c http_parser.c
OBJS += $(addprefix $(OUTDIR)/,$(CSRCS:.c=$(OBJ_SUFFIX)))

INCLUDE_DIRS += ../include

ifeq ($(AE), POLL)
	CPPFLAGS += -DHAVE_POLL
else
	ifeq ($(PLATFORM), __DARWIN__)
  	CPPFLAGS += -DHAVE_KQUEUE
	else ifeq ($(PLATFORM), __LINUX__)
		CPPFLAGS += -DHAVE_EPOLL
	endif
endif

LIB_NAME = inginx

include $(BUILD_DIR)/make.rules
