# No copyright. Vladislav Alenik, 2024

#-------
# Files
#-------

ifeq ($(PROGRAM),)
error:
	@printf "$(BRED)Specify build target!\n"
endif

EXECUTABLE = build/$(PROGRAM)

# By default, build executable:
# NOTE: first target in the file is the default.
default: $(EXECUTABLE)

#-----------------------
# Dependency management
#-----------------------

# Liburing:
LIBURING_LIB_DIR = $(abspath liburing/src/)
LIBURING_INCLUDE = $(abspath liburing/src/include)

LIBURING_SO = $(LIBURING_LIB_DIR)/liburing.so.2.6

LINK_TO_LIBURING = -L$(LIBURING_LIB_DIR) -Wl,-rpath=$(LIBURING_LIB_DIR) -luring

$(LIBURING_SO):
	-git clone https://github.com/axboe/liburing.git
	cd liburing && ./configure
	cd liburing && $(MAKE)

# Libaio:
LIBAIO_LIB_DIR = $(abspath libaio/src/)
LIBAIO_INCLUDE = $(abspath libaio/src/)

LIBAIO_SO = $(LIBAIO_LIB_DIR)/libaio.so.1.0.1

LINK_TO_LIBAIO = -L$(LIBAIO_LIB_DIR) -Wl,-rpath=$(LIBAIO_LIB_DIR) -laio

$(LIBAIO_SO):
	-git clone https://github.com/crossbuild/libaio.git
	cd libaio && ./configure
	cd libaio && $(MAKE)

#-----------------------
# Compiler/linker flags
#-----------------------

CC = gcc

# Compiler flags:
CFLAGS = \
	-std=c2x \
	-Wall    \
	-Wextra  \
	-Werror \
	-I $(LIBURING_INCLUDE) \
	-I $(LIBAIO_INCLUDE)

# Linker flags:
LDFLAGS = -pthread -lrt

# Select build mode:
# NOTE: invoke with "DEBUG=1 make" or "make DEBUG=1".
ifeq ($(DEBUG),1)
	# Add default symbols:
	CFLAGS += -g
else
	# Enable link-time optimization:
	CFLAGS  += -flto
	LDFLAGS += -flto
endif

#--------
# Colors
#--------

# Use ANSI color codes:
BRED    = \033[1;31m
BGREEN  = \033[1;32m
BYELLOW = \033[1;33m
GREEN   = \033[1;35m
BCYAN   = \033[1;36m
RESET   = \033[0m

#-------------
# Copied file
#-------------

DUMMY_SRC = build/dummy
DUMMY_DST = build/dummy_dst

create-src-file: $(DUMMY_SRC)

$(DUMMY_SRC):
	@mkdir -p build
	@dd if=/dev/zero of=$(DUMMY_SRC) bs=128M count=1
	@echo "AAA" >> $(DUMMY_SRC)

#-------------------
# Build/run process
#-------------------

build/%: %.c $(LIBURING_SO) $(LIBAIO_SO)
	@printf "$(BYELLOW)Building program $(BCYAN)$<$(RESET)\n"
	@mkdir -p build
	$(CC) $< $(CFLAGS) -o $@ $(LDFLAGS) $(LINK_TO_LIBURING) $(LINK_TO_LIBAIO)

run: $(EXECUTABLE) $(DUMMY_SRC)
	@./$(EXECUTABLE) $(DUMMY_SRC) $(DUMMY_DST)

# Timing command usage:
TIME_CMD    = /usr/bin/time
TIME_FORMAT = \
	"CPU Percentage: %P\nReal time: %e sec\nUser time: %U sec"

time: $(EXECUTABLE) $(DUMMY_SRC)
	@$(TIME_CMD) --quiet --format=$(TIME_FORMAT) $(EXECUTABLE) $(DUMMY_SRC) $(DUMMY_DST) | cat

#---------------
# Miscellaneous
#---------------

clean:
	@printf "$(BYELLOW)Cleaning build directory$(RESET)\n"
	@rm -rf build

# List of non-file targets:
.PHONY: run clean default
