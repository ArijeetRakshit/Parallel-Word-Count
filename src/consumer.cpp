#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h>
#include <unordered_map>          
#include <cstring>
#include <csignal>
#include <atomic>
#include <cerrno>
#include <algorithm>    
#include <vector>       
#include <fstream>      

#include "common.h"

using namespace std;

atomic_bool running(true);

void signal_handler(int signum) {
    cout << "\nConsumer: SIGINT received (" << signum << "). Shutting down gracefully..." << endl;
    running.store(false);
}

int cleanUp(int shm_fd, SharedWordBuffer* wordBuffer, sem_t* sem_empty, sem_t* sem_full, sem_t* sem_mutex, bool isError = false) {
    if (sem_empty != SEM_FAILED && sem_close(sem_empty) == -1)
        perror("Consumer: sem_close SEM_EMPTY_NAME failed");

    if (sem_full != SEM_FAILED && sem_close(sem_full) == -1)
        perror("Consumer: sem_close SEM_FULL_NAME failed");

    if (sem_mutex != SEM_FAILED && sem_close(sem_mutex) == -1)
        perror("Consumer: sem_close SEM_MUTEX_NAME failed");

    if (wordBuffer != MAP_FAILED && munmap(wordBuffer, sizeof(SharedWordBuffer)) == -1)
        perror("Consumer: munmap failed");

    if (shm_fd != -1 && close(shm_fd) == -1)
        perror("Consumer: close shm_fd failed");

    return isError ? 1 : 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) { // Expects 2 arguments
        cerr << "Usage: " << argv[0] << " <total_expected_producers> <consumer_id>" << endl;
        return 1;
    }
    int total_expected_producers = atoi(argv[1]);
    if (total_expected_producers <= 0) {
        cerr << "Error: total_expected_producers must be a positive integer." << endl;
        return 1;
    }
    string consumer_id = argv[2]; // Unique ID for this consumer

    cout << "Word Consumer Process Started (ID: " << consumer_id << "). Expecting EOFs from " << total_expected_producers << " producers." << endl;

    // Register signal handler for graceful shutdown
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("Consumer: signal failed");
        return 1;
    }

    int shm_fd = -1;
    SharedWordBuffer* wordBuffer = (SharedWordBuffer*) MAP_FAILED;
    sem_t* sem_empty = SEM_FAILED;
    sem_t* sem_full = SEM_FAILED;
    sem_t* sem_mutex = SEM_FAILED;
    unordered_map<string, int> wordCounts; // Map to store word frequencies (local to this consumer)
    int words_processed = 0;

    // Open Shared Memory
    shm_fd = shm_open(SHARED_MEM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Consumer: shm_open failed");
        cerr << "Consumer: Ensure producer process(es) are running and initialized the shared memory." << endl;
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Map Shared Memory to Process Address Space
    wordBuffer = (SharedWordBuffer*) mmap(0, sizeof(SharedWordBuffer), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (wordBuffer == MAP_FAILED) {
        perror("Consumer: mmap failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Open Semaphores
    sem_empty = sem_open(SEM_EMPTY_NAME, 0); // Open existing semaphore
    if (sem_empty == SEM_FAILED) {
        perror("Consumer: sem_open SEM_EMPTY_NAME failed");
        cerr << "Consumer: Ensure producer process(es) have created the semaphores." << endl;
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    sem_full = sem_open(SEM_FULL_NAME, 0); // Open existing semaphore
    if (sem_full == SEM_FAILED) {
        perror("Consumer: sem_open SEM_FULL_NAME failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    sem_mutex = sem_open(SEM_MUTEX_NAME, 0); // Open existing semaphore
    if (sem_mutex == SEM_FAILED) {
        perror("Consumer: sem_open SEM_MUTEX_NAME failed");
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
    }

    // Wait for shared memory to be initialized by a producer if it's not already
    cout << "Consumer (ID: " << consumer_id << "): Waiting for shared memory initialization..." << endl;
    while (running.load() && !wordBuffer->initialized.load()) {
        sleep(1);
    }

    if (!running.load())
        return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);

    // Read and Analyze Words
    while (running.load()) {
        // Check if all producers have finished (and sent their EOFs)
        if (wordBuffer->eof_signals_received.load() >= total_expected_producers && wordBuffer->active_producers_count.load() == 0) {
            cout << "Consumer (ID: " << consumer_id << "): All producers finished and signaled EOF. Exiting." << endl;
            running.store(false); // Set running to false to break the loop
            break;
        }

        if (sem_wait(sem_full) == -1) {
            if (errno == EINTR) {
                if (!running.load()) 
                    break; // Signal received, gracefully exit loop
                continue;
            }
            perror("Consumer: sem_wait SEM_FULL_NAME failed");
            return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
        }

        if (sem_wait(sem_mutex) == -1) {
            if (errno == EINTR) {
                if (!running.load()) {
                    sem_post(sem_full); // Release previously acquired sem_full
                    break;
                }
                continue;
            }
            perror("Consumer: sem_wait SEM_MUTEX_NAME failed");
            sem_post(sem_full); // If mutex fails, release sem_full to avoid deadlock
            return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex, true);
        }

        WordEntry currentEntry = wordBuffer->entries[wordBuffer->tail];
        wordBuffer->tail = (wordBuffer->tail + 1) % MAX_WORD_ENTRIES;

        if (sem_post(sem_mutex) == -1) {
            perror("Consumer: sem_post SEM_MUTEX_NAME failed");
        }

        if (sem_post(sem_empty) == -1) {
            perror("Consumer: sem_post SEM_EMPTY_NAME failed");
        }

        if (strcmp(currentEntry.word, EOF_SIGNAL_WORD) == 0) {
            cout << "Consumer (ID: " << consumer_id << "): Received an EOF signal. Current total EOFs received: " << wordBuffer->eof_signals_received.load() + 1 << endl;
            wordBuffer->eof_signals_received++;

            // If all expected producers have sent EOFs, we can stop
            if (wordBuffer->eof_signals_received.load() >= total_expected_producers && wordBuffer->active_producers_count.load() == 0) {
                cout << "Consumer (ID: " << consumer_id << "): All expected EOFs received and no active producers. Terminating." << endl;
                running.store(false);
            }
            continue; // Don't process the EOF word as a regular word
        }

        cout << "Consumer (ID: " << consumer_id << "): Read word [" << currentEntry.word << "]" << endl;
        words_processed++;

        // count word frequency
        wordCounts[string(currentEntry.word)]++;

        if (running.load()) {
            usleep(rand() % 70000 + 10000); // Simulate some work (10-80ms)
        }
    }

    cout << "Consumer (ID: " << consumer_id << "): Shutting down. Total words processed: " << words_processed << endl;

    // Write local word counts to a unique file ---
    string output_filename = "consumer_output_" + consumer_id + ".txt";
    ofstream outfile(output_filename);
    if (!outfile.is_open()) {
        perror(("Consumer (ID: " + consumer_id + "): Failed to open output file " + output_filename).c_str());
    } else {
        cout << "Consumer (ID: " << consumer_id << "): Writing word counts to " << output_filename << endl;
        for (const auto& [word, count] : wordCounts) {
            outfile << word << "\t" << count << endl; // Write word and count, tab-separated
        }
        outfile.close();
    }
    return cleanUp(shm_fd, wordBuffer, sem_empty, sem_full, sem_mutex);
}