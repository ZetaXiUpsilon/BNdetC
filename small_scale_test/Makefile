# Makefile for SAT Solver with Concurrency
# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2 -pthread
LDFLAGS = -pthread

# Target executables
TARGET1 = method1
TARGET2 = method2
TARGET3 = determ
TARGET4 = verify

# Source files
SRC1 = method1.cpp
SRC2 = method2.cpp
SRC3 = determ.cpp
SRC4 = verify.cpp

# Default target
all: $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4)

# Build method1
$(TARGET1): $(SRC1)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Build method2
$(TARGET2): $(SRC2)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	
# Build method3
$(TARGET3): $(SRC3)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)
	
# Build method4
$(TARGET4): $(SRC4)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

# Build with debug symbols
debug: CXXFLAGS += -g -DDEBUG
debug: clean all

# Build with optimization (default)
release: CXXFLAGS += -O3
release: clean all

# Build with profiling
profile: CXXFLAGS += -pg
profile: LDFLAGS += -pg
profile: clean all

# Clean object files and executables
clean:
	rm -f $(TARGET1) $(TARGET2) $(TARGET3) $(TARGET4) *.o *.gch

# Clean and rebuild
rebuild: clean all

# Run method1 on a test file
test1: $(TARGET1)
	./$(TARGET1) $(FILE)

# Run method2 on a test file
test2: $(TARGET2)
	./$(TARGET2) $(FILE)
	
# Run determ on a test file
test_determ: $(TARGET3)
	./$(TARGET3) $(FILE)

# Run all methods on a test file and compare
test: $(TARGET1) $(TARGET2) $(TARGET3)
	@echo "======= Deterministic ========"
	./$(TARGET3) $(FILE)
	@echo ""
	@echo "========== Method 1 =========="
	./$(TARGET1) $(FILE)
	@echo ""
	@echo "========== Method 2 =========="
	./$(TARGET2) $(FILE)

# Check if files exist
check:
	@if [ ! -f $(SRC1) ]; then \
		echo "Error: $(SRC1) not found!"; \
		exit 1; \
	fi
	@if [ ! -f $(SRC2) ]; then \
		echo "Error: $(SRC2) not found!"; \
		exit 1; \
	fi
	@if [ ! -f $(SRC3) ]; then \
		echo "Error: $(SRC3) not found!"; \
		exit 1; \
	fi
	@if [ ! -f $(SRC4) ]; then \
		echo "Error: $(SRC4) not found!"; \
		exit 1; \
	fi

# Help information
help:
	@echo "SAT Solver Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make            - Build both solvers (default)"
	@echo "  make method1    - Build only method1"
	@echo "  make method2    - Build only method2"
	@echo "  make debug      - Build with debug symbols (-g -DDEBUG)"
	@echo "  make release    - Build with maximum optimization (-O3)"
	@echo "  make profile    - Build with profiling support (-pg)"
	@echo "  make clean      - Remove executables and object files"
	@echo "  make rebuild    - Clean and rebuild"
	@echo "  make test FILE=file.cnf      - Run both solvers on test.cnf"
	@echo "  make test1 FILE=file.cnf     - Run method1 on test.cnf"
	@echo "  make test2 FILE=file.cnf     - Run method2 on test.cnf"
	@echo "  make test_determ FILE=file.cnf        - Run determ on test.cnf"
	@echo "  make help       - Show this help message"
	@echo ""
	@echo "Examples:"
	@echo "  make run1 FILE=sample.cnf"
	@echo "  make debug && make test"

# Phony targets (not real files)
.PHONY: all debug release profile clean rebuild test test1 test2 test_determ check help

# Dependencies
$(TARGET1): $(SRC1)
$(TARGET2): $(SRC2)
$(TARGET3): $(SRC3)
$(TARGET4): $(SRC4)

# Ensure source files exist
all: check

# Set default goal
.DEFAULT_GOAL := all
