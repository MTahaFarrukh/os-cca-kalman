# ==============================================================================
# Makefile for os-cca-kalman (Linux / Ubuntu g++ Shortcut)
# Course: CT-353 Operating Systems - Complex Computing Activity (CCA)
# Department of Computer Science & Information Technology, NED University
# ==============================================================================

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2 -Icommon -Ilinux -pthread
LDFLAGS ?= -pthread

BUILD_DIR = build_linux
BIN_DIR = bin_linux
DATA_DIR = data
RESULTS_DIR = results

OBJS_COMMON = $(BUILD_DIR)/kalman.o $(BUILD_DIR)/logger.o
OBJS_LINUX  = $(BUILD_DIR)/sys_proc.o $(BUILD_DIR)/sys_file.o $(BUILD_DIR)/sys_mem.o $(BUILD_DIR)/sys_ipc.o $(BUILD_DIR)/sys_perm.o

TARGETS = $(BIN_DIR)/sensor_gen $(BIN_DIR)/syscall_bench_linux $(BIN_DIR)/kalman_linux

.PHONY: all clean run_sensor run_bench run_kalman dirs help

all: dirs $(TARGETS)

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR) $(DATA_DIR) $(RESULTS_DIR)

# Common objects
$(BUILD_DIR)/kalman.o: common/kalman.cpp common/kalman.hpp
	$(CXX) $(CXXFLAGS) -c common/kalman.cpp -o $@

$(BUILD_DIR)/logger.o: common/logger.cpp common/logger.hpp
	$(CXX) $(CXXFLAGS) -c common/logger.cpp -o $@

# Linux system call objects
$(BUILD_DIR)/sys_proc.o: linux/sys_proc.cpp linux/sys_linux.hpp common/bench.hpp
	$(CXX) $(CXXFLAGS) -c linux/sys_proc.cpp -o $@

$(BUILD_DIR)/sys_file.o: linux/sys_file.cpp linux/sys_linux.hpp
	$(CXX) $(CXXFLAGS) -c linux/sys_file.cpp -o $@

$(BUILD_DIR)/sys_mem.o: linux/sys_mem.cpp linux/sys_linux.hpp
	$(CXX) $(CXXFLAGS) -c linux/sys_mem.cpp -o $@

$(BUILD_DIR)/sys_ipc.o: linux/sys_ipc.cpp linux/sys_linux.hpp
	$(CXX) $(CXXFLAGS) -c linux/sys_ipc.cpp -o $@

$(BUILD_DIR)/sys_perm.o: linux/sys_perm.cpp linux/sys_linux.hpp
	$(CXX) $(CXXFLAGS) -c linux/sys_perm.cpp -o $@

# Executables
$(BIN_DIR)/sensor_gen: common/sensor_gen.cpp $(OBJS_COMMON)
	$(CXX) $(CXXFLAGS) -o $@ common/sensor_gen.cpp $(OBJS_COMMON) $(LDFLAGS)

$(BIN_DIR)/syscall_bench_linux: linux/syscall_bench_linux.cpp $(OBJS_COMMON) $(OBJS_LINUX)
	$(CXX) $(CXXFLAGS) -o $@ linux/syscall_bench_linux.cpp $(OBJS_COMMON) $(OBJS_LINUX) $(LDFLAGS)

$(BIN_DIR)/kalman_linux: linux/kalman_linux.cpp $(OBJS_COMMON) $(OBJS_LINUX)
	$(CXX) $(CXXFLAGS) -o $@ linux/kalman_linux.cpp $(OBJS_COMMON) $(OBJS_LINUX) $(LDFLAGS)

run_sensor: $(BIN_DIR)/sensor_gen
	./$(BIN_DIR)/sensor_gen $(DATA_DIR)

run_bench: $(BIN_DIR)/syscall_bench_linux
	./$(BIN_DIR)/syscall_bench_linux

run_kalman: $(BIN_DIR)/kalman_linux
	./$(BIN_DIR)/kalman_linux --scaling

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned Linux build artifacts."

help:
	@echo "Available targets:"
	@echo "  make              - Build sensor_gen, syscall_bench_linux, and kalman_linux"
	@echo "  make run_sensor   - Generate synthetic sensor and truth datasets"
	@echo "  make run_bench    - Run 15-syscall demo and benchmark suite"
	@echo "  make run_kalman   - Run parallel Kalman filter scaling benchmarks (N=1,2,4,8)"
	@echo "  make clean        - Remove build artifacts and binaries"
