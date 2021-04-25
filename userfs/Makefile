########
#
#   Simplified Makefile, for each source we call a function (make-goal) to
#	compile, this is done in the last line of this file.
#
########

#CC=clang-3.8
#CC=gcc
CXX=g++ --std=c++11
CC=gcc

########
#  List of all modules' directories
########
MODULES   := io concurrency config filesystem global ds log \
	userfs posix storage threadpool

########
#  List of all modules' directories
########

########
#  All modules' directories in src and build
########
SRC_DIR   := $(addprefix src/,$(MODULES))
BUILD_DIR := $(addprefix build/,$(MODULES))

########
#  Source and Object files in their  module directories
########
SRC       := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c))
OBJ       := $(patsubst src/%.c,build/%.o,$(SRC))

########
#  Lib directories
########
NVML_DIR := $(abspath $(CURDIR)/lib/nvml/src)
JEMALLOC_DIR := $(abspath $(CURDIR)/lib/jemalloc-4.5.0)

INCLUDES  := $(addprefix -I,src/ $(NVML_DIR)/include)

LD_FLAGS = -lpthread -lm -L$(NVML_DIR)/nondebug/ -lpmem -lrt #-Wl,-fuse-ld=gold

#USERFS_FLAGS = -DUSE_SLAB -DLIBFS -DUSERFS_INFO
USERFS_FLAGS = -DLIBFS
#USERFS_FLAGS = -DLIBFS -DUSERFS_PRINT -DUSERFS_INFO
#USERFS_FLAGS = -DLIBFS 
#USERFS_FLAGS += -DCONCURRENT 
#USERFS_FLAGS += -DCONCURRENT -DEACHPOOL
USERFS_FLAGS += -DINVALIDATION
USERFS_FLAGS += -DKLIB_HASH
#USERFS_FLAGS += -DUSE_SSD
#USERFS_FLAGS += -DUSE_HDD
#USERFS_FLAGS += -DUSERFS_LOG

DEBUG = -g3 -O0 
#DEBUG = -O0 -gdwarf-2 -g3 -rdynamic
#DEBUG = -g -Ofast
#DEBUG = -O3

########
#  vpath and compile function for each file
########
vpath %.c $(SRC_DIR)

define make-goal
$1/%.o: %.c 
	$(CC) $(INCLUDES) $(USERFS_FLAGS) $(DEBUG) -fPIC -c $$< -o $$@ -DLIBFS
endef

########
#  Phony targets
########
.PHONY: all checkdirs clean

all: checkdirs libuserfs
checkdirs: $(BUILD_DIR)
	@mkdir -p bin

clean:
	@rm -rf $(BUILD_DIR)
	#make -C src/storage/spdk/ clean

########
#  Create dirs recursively
########
$(BUILD_DIR):
	@mkdir -p $@

########
#  Targets
########
#build/libuserfs.a: $(OBJ)
libuserfs: $(OBJ)
	ar cr build/libuserfs.a $(OBJ) 
	$(CXX) -shared $(DEBUG) -o build/libuserfs.so $(OBJ) $(LD_FLAGS) -DLIBFS $(USERFS_FLAGS) -L$(JEMALLOC_DIR)/lib -Wl,-rpath=$(abspath $(JEMALLOC_DIR)/lib) -ljemalloc 


########
#  Compile each source into an object
########
$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))
