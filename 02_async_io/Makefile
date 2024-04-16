# No copyright. Vladislav Alenik, 2024

#-----------------------
# Compiler/linker flags
#-----------------------

CC = gcc

# Compiler flags:
CFLAGS = \
	-std=c2x \
	-Wall    \
	-Wextra  \
	-Werror

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

#-------
# Files
#-------

ifeq ($(PROGRAM),)
error:
	@printf "$(BRED)Specify build target!\n"
endif

EXECUTABLE = build/$(PROGRAM)

#-------------
# Copied file
#-------------

DUMMY_SRC = build/dummy
DUMMY_DST = build/dummy_dst

create-src-file: $(DUMMY_SRC)

$(DUMMY_SRC):
	@mkdir -p build
	@dd if=/dev/zero of=$(DUMMY_SRC) bs=512M count=1
	@echo "AAA" >> $(DUMMY_SRC)

#-------------------
# Build/run process
#-------------------

# By default, build executable:
# NOTE: first target in the file is the default.
default: $(EXECUTABLE)

build/%: %.c
	@printf "$(BYELLOW)Building program $(BCYAN)$^$(RESET)\n"
	@mkdir -p build
	$(CC) $< $(CFLAGS) -o $@ $(LDFLAGS)

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
