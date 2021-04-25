CC = gcc -std=c99
#CC = c99
EXE = iotest io_test createfile rw_test read_test rw_test1 diraccess 
#$(info $(EXE))

CUR_DIR = $(shell pwd)
GLIBC_DIR = $(CUR_DIR)/../../shim/glibc-build/
LIBFS_DIR = $(CUR_DIR)/../build/
NVML_DIR = $(LIBFS_DIR)/../lib/nvml/src/nondebug/
CUCKOO_DIR = $(LIBFS_DIR)/../lib/cuckoofilter

INCLUDES= $(LIBFS_DIR)/../src -I$(CUR_DIR)/../lib/nvml/src/include

$DPDK_VER := 16.07.2
DPDK_DIR := $(abspath $(CURDIR)/../lib/dpdk-$(DPDK_VER)/x86_64-native-linuxapp-gcc)

CFLAGS= -I $(DPDK_DIR)/include -I$(INCLUDES)

LDFLAGS = -Wl,-rpath=$(abspath $(LIBFS_DIR)) \
		  -Wl,-rpath=$(abspath $(NVML_DIR)) \
		  -lpthread -lrt -lm -lssl -lcrypto

all: $(EXE)

%.o: %.c
	$(CC) $(CFLAGS) -c -g $< -o $@

%.o: %.cc
	$(CXX) -std=c++11 $(CFLAGS) -c -g $< -o $@

time_stat.o: time_stat.c
	$(CC) time_stat.c -c -o time_stat.o -D_BSD_SOURCE


diraccess: diraccess.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS)

rw_test: rw_test.c
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS)

rw_test1: rw_test1.c
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS)

read_test: read_test.c
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS)

createfile: createfile.cc
	$(CXX) -std=c++11 -g -o $@ $^ -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(LDFLAGS)

iotest: iotest.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS) 
	$(CXX) -std=c++11 -g -Ofast -o $(addsuffix .normal, $@) $^ $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR))

io_test: io_test.cc time_stat.o thread.cc
	$(CXX) -std=c++11 -g -Ofast -o $@ $^  -I$(INCLUDES) -L$(LIBFS_DIR) -luserfs -DUSERFS $(CFLAGS) $(LDFLAGS) 
	$(CXX) -std=c++11 -g -Ofast -o $(addsuffix .normal, $@) $^ $(CFLAGS) $(DAX_OBJ) -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR))

dir_test: dir_test.c
	$(CC) -O0 -g -o $@ $^ $(CFLAGS) -L$(LIBFS_DIR) -luserfs -lm -lrt -L$(NVML_DIR) -lpmem -lpthread -lm -lrt -Wl,-rpath=$(abspath $(NVML_DIR)) -I$(INCLUDES) $(LDFLAGS)


clean:
	rm -rf *.o $(EXE) *.normal

