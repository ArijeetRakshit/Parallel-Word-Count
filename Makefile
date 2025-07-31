CXX = g++
CXXFLAGS = -Wall -std=c++17 -pthread # Using C++17 for std::atomic_bool robustness, -pthread for semaphores and atomics
LDFLAGS = -lrt # Link against the real-time library for POSIX semaphores and shared memory

BIN_DIR = bin
SRC_DIR = src

# Define source files
PRODUCER_SRC = $(SRC_DIR)/producer.cpp
CONSUMER_SRC = $(SRC_DIR)/consumer.cpp
AGGREGATOR_SRC = $(SRC_DIR)/aggregator.cpp # NEW: Aggregator source
COMMON_HDR = $(SRC_DIR)/common.h

# Define executables
PRODUCER_BIN = $(BIN_DIR)/producer
CONSUMER_BIN = $(BIN_DIR)/consumer
AGGREGATOR_BIN = $(BIN_DIR)/aggregator # NEW: Aggregator executable

all: $(PRODUCER_BIN) $(CONSUMER_BIN) $(AGGREGATOR_BIN) # NEW: Add aggregator to 'all'

# Create bin directory if it doesn't exist
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Rule to build the producer executable
$(PRODUCER_BIN): $(PRODUCER_SRC) $(COMMON_HDR) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Rule to build the consumer executable
$(CONSUMER_BIN): $(CONSUMER_SRC) $(COMMON_HDR) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# NEW Rule to build the aggregator executable
$(AGGREGATOR_BIN): $(AGGREGATOR_SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	@echo "Cleaning compiled binaries..."
	rm -f $(PRODUCER_BIN) $(CONSUMER_BIN) $(AGGREGATOR_BIN) # NEW: Remove aggregator binary
	rm -rf $(BIN_DIR)
	@echo "Attempting to remove shared memory and semaphores (requires sudo for /dev/shm cleanup)..."
	-sudo rm -f /dev/shm/word_shared_memory
	-sudo rm -f /dev/shm/sem.word_sem_empty
	-sudo rm -f /dev/shm/sem.word_sem_full
	-sudo rm -f /dev/shm/sem.word_sem_mutex
	@echo "Removing individual consumer output files and final aggregated file..."
	rm -f consumer_output_*.txt # NEW: Remove individual consumer output files
	rm -f aggregated_word_counts.txt # NEW: Remove final aggregated output
	@echo "Cleanup complete."

.PHONY: all clean