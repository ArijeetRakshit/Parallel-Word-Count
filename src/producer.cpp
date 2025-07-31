#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>
#include <ctime>
#include <cstdlib>
#include <csignal>
#include <atomic>
#include <cerrno>
#include <fstream>      
#include <cstring>
#include <algorithm>    
#include <cctype>       

#include "common.h"

using namespace std;

// Flag for graceful shutdown
atomic_bool running(true);

void signal_handler(int signum) {
    cout << "\nProducer: SIGINT received (" << signum << "). Shutting down gracefully..." << endl;
    running.store(false);
}

string cleanWord(const string& rawWord) {
    string cleaned;
    for (char c : rawWord) {
        if (isalnum(static_cast<unsigned char>(c))) { 
            cleaned += tolower(static_cast<unsigned char>(c)); 
        }
    }
    return cleaned;
}

int cleanUp(int shm_fd, SharedWordBuffer* wordBuffer, sem_t* sem_empty, sem_t* sem_full, sem_t* sem_mutex, bool isError = false) {
    if (sem_empty != SEM_FAILED && sem_close(sem_empty) == -1)
        perror("Producer: sem_close SEM_EMPTY_NAME failed");

    if (sem_full != SEM_FAILED && sem_close(sem_full) == -1)
        perror("Producer: sem_close SEM_FULL_NAME failed");

    if (sem_mutex != SEM_FAILED && sem_close(sem_mutex) == -1)
        perror("Producer: sem_close SEM_MUTEX_NAME failed");

    if (wordBuffer != MAP_FAILED && munmap(wordBuffer, sizeof(SharedWordBuffer)) == -1)
        perror("Producer: munmap failed");

    if (shm_fd != -1 && close(shm_fd) == -1)
        perror("Producer: close shm_fd failed");

    return isError ? 1 : 0;
}


void send_eof_signal(SharedWordBuffer* wordBuffer, sem_t* sem_empty, sem_t* sem_full, sem_t* sem_mutex) {
    if (sem_wait(sem_empty) == -1) {
        if (errno == EINTR && !running.load()) // Interrupted by SIGINT during shutdown
            return;
        perror("Producer: sem_wait for EOF signal failed");
        return;
    }

    if (sem_wait(sem_mutex) == -1) {
        if (errno == EINTR && !running.load()) {
            sem_post(sem_empty); // Release empty slot if interrupted during shutdown
            return;
        }
        perror("Producer: sem_wait mutex for EOF signal failed");
        sem_post(sem_empty); // Release empty slot if mutex acquisition fails
        return;
    }

    // Write EOF signal
    WordEntry eofEntry;
    strncpy(eofEntry.word, EOF_SIGNAL_WORD, MAX_WORD_LENGTH - 1);
    eofEntry.word[MAX_WORD_LENGTH - 1] = '\0';

    wordBuffer->entries[wordBuffer->head] = eofEntry;
    wordBuffer->head = (wordBuffer->head + 1) % MAX_WORD_ENTRIES;

    if (sem_post(sem_mutex) == -1)
        perror("Producer: sem_post mutex for EOF signal failed");

    // Signal that a new item (EOF) is available
    if (sem_post(sem_full) == -1)
        perror("Producer: sem_post full for EOF signal failed");
}


int main(int argc, char* argv[]) {
    if (argc < 2) {
        cerr << "Usage: " << argv[0] << " <input_file.txt>" << endl;
        return 1;
    }
    const char* inputFileName = argv[1];

    cout << "Word Producer Process Started. Reading from: " << inputFileName << endl;

    // Register signal handler for graceful shutdown
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("Producer: signal failed");
        return 1;
    }

    int shm_fd = -1;
    SharedWordBuffer* wordBuffer = static_cast<SharedWordBuffer*> MAP_FAILED;
    sem_t* sem_empty = SEM_FAILED;
    sem_t* sem_full = SEM_FAILED;
    sem_t* sem_mutex = SEM_FAILED;
    bool expected_initialized = false; // For atomic_bool compare_exchange_strong

    // Open Shared Memory
    shm_fd = shm_open(SHARED_MEM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Producer: shm_open failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Configure Shared Memory Size
    if (ftruncate(shm_fd, sizeof(SharedWordBuffer)) == -1) {
        perror("Producer: ftruncate failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Map Shared Memory to Process Address Space
    wordBuffer = (SharedWordBuffer*) mmap(0, sizeof(SharedWordBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (wordBuffer == MAP_FAILED) {
        perror("Producer: mmap failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Open Semaphores
    sem_empty = sem_open(SEM_EMPTY_NAME, O_CREAT, 0666, MAX_WORD_ENTRIES);
    if (sem_empty == SEM_FAILED) {
        perror("Producer: sem_open SEM_EMPTY_NAME failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    sem_full = sem_open(SEM_FULL_NAME, O_CREAT, 0666, 0);
    if (sem_full == SEM_FAILED) {
        perror("Producer: sem_open SEM_FULL_NAME failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    sem_mutex = sem_open(SEM_MUTEX_NAME, O_CREAT, 0666, 1);
    if (sem_mutex == SEM_FAILED) {
        perror("Producer: sem_open SEM_MUTEX_NAME failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Robust Shared Memory Initialization
    if (wordBuffer->initialized.compare_exchange_strong(expected_initialized, true)) {
        cout << "Producer: Initializing shared word buffer for the first time." << endl;
        wordBuffer->head = 0;
        wordBuffer->tail = 0;
        wordBuffer->active_producers_count.store(0); // Initialize count
        wordBuffer->eof_signals_received.store(0); // Initialize EOF count
    } else {
        cout << "Producer: Shared word buffer already initialized by another process." << endl;
    }

    wordBuffer->active_producers_count++;
    cout << "Producer: Active producers count: " << wordBuffer->active_producers_count.load() << endl;


    // Open input file
    ifstream inputFile(inputFileName);
    if (!inputFile.is_open()) {
        perror(("Producer: Failed to open input file: " + string(inputFileName)).c_str());
        // Decrement count if file can't be opened, as this producer won't contribute words
        wordBuffer->active_producers_count--;
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    string rawWord;
    int words_produced = 0;

    // Read from file and Write Words to Shared Memory
    while (running.load() && (inputFile >> rawWord)) {
        string cleaned = cleanWord(rawWord);

        if (cleaned.empty()) { // Skip empty strings after cleaning
            continue;
        }

        if (sem_wait(sem_empty) == -1) {
            if (errno == EINTR) {
                if (!running.load()) 
                    break;
                continue;
            }
            perror("Producer: sem_wait SEM_EMPTY_NAME failed");
            inputFile.close();
            return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
        }

        if (sem_wait(sem_mutex) == -1) {
            if (errno == EINTR) {
                if (!running.load()) {
                    sem_post(sem_empty); // Release previously acquired sem_empty
                    break;
                }
                continue;
            }
            perror("Producer: sem_wait SEM_MUTEX_NAME failed");
            sem_post(sem_empty); // If mutex fails, release sem_empty to avoid deadlock
            inputFile.close();
            return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
        }

        WordEntry newEntry;
        strncpy(newEntry.word, cleaned.c_str(), MAX_WORD_LENGTH  - 1);
        newEntry.word[MAX_WORD_LENGTH - 1] = '\0';

        wordBuffer->entries[wordBuffer->head] = newEntry;
        wordBuffer->head = (wordBuffer->head + 1) % MAX_WORD_ENTRIES;

        cout << "Producer: Wrote word [" << newEntry.word << "]" << endl;
        words_produced++;

        if (sem_post(sem_mutex) == -1)
            perror("Producer: sem_post SEM_MUTEX_NAME failed");

        if (sem_post(sem_full) == -1) {
            perror("Producer: sem_post SEM_FULL_NAME failed");
        }

        // Simulate some work, allowing consumer to run
        if (running.load()) {
            usleep(rand() % 50000 + 10000); // Sleep for 10-60ms
        }
    }

    inputFile.close();

    // Send this producer's EOF signal
    if (running.load()) { // Only send if not interrupted by SIGINT
        cout << "Producer: Finished reading file. Sending my EOF signal..." << endl;
        send_eof_signal(wordBuffer, sem_empty, sem_full, sem_mutex);
    }

    int remaining_producers = --wordBuffer->active_producers_count;
    cout << "Producer: My file is done. Remaining active producers: " << remaining_producers << endl;

    // If this is the last producer, send multiple EOF signals to unblock all consumers
    if (running.load() && remaining_producers == 0) {
        cout << "Producer: I am the last producer. Sending multiple EOF signals to unblock consumers." << endl;
        for (int i = 0; i < MAX_WORD_ENTRIES; ++i) { // Send enough EOFs to fill the buffer
            send_eof_signal(wordBuffer, sem_empty, sem_full, sem_mutex);
            // Small delay to allow consumers to pick up if they are very fast
            usleep(10000);
        }
    }


    cout << "Producer Process Shutting Down. Total words produced: " << words_produced << endl;
    return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex);
}