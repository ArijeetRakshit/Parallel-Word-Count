#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <atomic>


const int MAX_WORD_LENGTH = 255;
const int MAX_WORD_ENTRIES = 10; // Number of word entries the shared memory will hold

struct WordEntry {
    char word[MAX_WORD_LENGTH];
};

struct SharedWordBuffer {
    std::atomic_bool initialized; // Flag to ensure one-time initialization of the buffer
    WordEntry entries[MAX_WORD_ENTRIES];
    int head; // Index of the next available slot for writing (producer)
    int tail; // Index of the next entry to be read (consumer)

    // Track active producers and EOF signals for graceful multi-producer shutdown
    std::atomic_int active_producers_count; // Number of producers currently running
    std::atomic_int eof_signals_received;   // Count of EOF signals received from producers
};

// IPC Resource Names 
const char* SHARED_MEM_NAME = "/word_shared_memory";
const char* SEM_EMPTY_NAME = "/word_sem_empty";
const char* SEM_FULL_NAME = "/word_sem_full";
const char* SEM_MUTEX_NAME = "/word_sem_mutex";

// Special word to signal end of file from a producer
const char* EOF_SIGNAL_WORD = "__EOF__";

#endif