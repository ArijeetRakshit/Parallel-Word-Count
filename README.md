# Parallel Word Counter

This project demonstrates **Inter-Process Communication (IPC)** and **Shared Memory** concepts in C++ using POSIX APIs to build a distributed word counting system. It simulates a scenario where "Producer" processes read large text files, tokenize them into words, and write these words to a shared memory segment. "Consumer" processes then read these words from shared memory, count their frequencies, and output individual summaries, which are finally aggregated by a dedicated "Aggregator" process.

It includes:
* **Robust Error Handling:** Comprehensive checks for system call failures with appropriate error messages and cleanup.
* **Graceful Shutdown:** Processes can be terminated gracefully using `Ctrl+C` (SIGINT), ensuring proper resource release.
* **Circular Buffer:** The shared memory buffer management relies purely on POSIX semaphores (`SEM_EMPTY_NAME` and `SEM_FULL_NAME`) to manage empty and full slots, and `head`/`tail` pointers for data access.
* **Multiple Producers/Consumers Support:** The semaphore-based synchronization inherently supports multiple producer and consumer instances concurrently accessing the shared buffer.
* **Final Aggregation:** A dedicated C++ aggregator process combines word counts from all consumers into a single, sorted output.

---

## Key Concepts Demonstrated:

* **Inter-Process Communication (IPC):** Communication between independent processes.
* **Shared Memory (`shm_open`, `mmap`):** A high-performance IPC mechanism allowing processes to directly access a common region of RAM.
* **POSIX Named Semaphores (`sem_open`, `sem_wait`, `sem_post`, `sem_unlink`):** Synchronization primitives used to control access to shared resources and prevent race conditions.
    * **Counting Semaphores:** `SEM_EMPTY_NAME` (tracks empty slots for producers) and `SEM_FULL_NAME` (tracks filled slots for consumers).
    * **Binary Semaphore (Mutex):** `SEM_MUTEX_NAME` for ensuring mutual exclusion during critical section access to the shared buffer.
* **Producer-Consumer Problem:** A classic concurrency problem solved using shared memory and semaphores.
* **System Programming:** Interaction with low-level operating system functionalities.
* **Signal Handling:** For graceful termination (`SIGINT`).
* **Atomic Operations (`std::atomic_bool`, `std::atomic<int>`):** For thread-safe flags and counters across processes (e.g., buffer initialization, active producer count, EOF signals).
* **Distributed Processing:** Breaking down a large task (word counting) into smaller, parallelizable sub-tasks handled by multiple processes.

---

## Project Structure:

* `src/common.h`: Defines shared data structures (e.g., `WordEntry`, `SharedWordBuffer`) and IPC resource names. Includes `std::atomic` types for robust shared state management.
* `src/producer.cpp`: The producer process. Reads text from input files, tokenizes words, and writes them to shared memory. Implements error handling, graceful shutdown, and logic for sending `__EOF__` signals.
* `src/consumer.cpp`: The consumer process. Reads words from shared memory, counts their frequencies locally, and writes individual summaries to `consumer_output_*.txt` files. Implements error handling, graceful shutdown, and robust buffer initialization waiting.
* `src/aggregator.cpp`: The final aggregation process. Reads all `consumer_output_*.txt` files, sums up the word counts, sorts them, and writes the final comprehensive report to `aggregated_word_counts.txt`.
* `Makefile`: Automates the compilation and cleanup process.
* `README.md`: Project documentation.
* `input1.txt`, `input2.txt`,  `input3.txt` (example): Sample input text files for producers.

---

## How to Build and Run:

**Prerequisites:**

* A C++ compiler (g++ recommended, supporting C++17 or newer for `std::atomic_bool` and `std::filesystem`).
* A Unix-like operating system (Linux, macOS) or WSL (Windows Subsystem for Linux).

**Steps:**

1.  **Clone the repository (or set up your project directory):**
    ```bash
    git clone https://github.com/ArijeetRakshit/Parallel-Word-Count.git
    cd Parallel-Word-Count
    ```
    Ensure your source files (`common.h`, `producer.cpp`, `consumer.cpp`, `aggregator.cpp`) are in a `src` subdirectory, and your `Makefile` is in the root.

2.  **Place Input Files:**
    Already there are 3 input files, if you want you can create one or more text files (e.g., `input4.txt`, `input5.txt`) in the root directory of your project. These will be read by the producer processes. Each producer reads one input text file.

3.  **Compile the project:**
    ```bash
    make
    ```
    This will create executables in the `bin/` directory: `bin/producer`, `bin/consumer`, and `bin/aggregator`.

4.  **Clean up previous runs (IMPORTANT!):**
    Before each fresh run, it's vital to clean up any leftover IPC resources and output files:
    ```bash
    make clean
    ```

5.  **Run the processes (Demonstrating Parallel Word Counting):**

    Open **multiple separate terminal windows** for each process.

    * **Terminal 1 (Run Producer 1):**
        ```bash
        ./bin/producer input1.txt 
        ```
    * **Terminal 2 (Run Producer 2 - Optional):**
        ```bash
        ./bin/producer input2.txt 
        ```
        *(Run as many producers as you have input files. The `&` runs them in the background, allowing you to use the same terminal for subsequent commands, but separate terminals are often clearer for observation.)*

    * **Terminal 3 (Run Consumer 1):**
        ```bash
        # Replace <TOTAL_NUM_PRODUCERS> with the actual count (e.g., 2)
        ./bin/consumer <TOTAL_NUM_PRODUCERS> 1  # '1' is a unique ID for this consumer
        ```
        *(The first argument `<TOTAL_NUM_PRODUCERS>` is crucial for the consumer to know when to expect all EOFs. The second argument `1` is a unique ID for this consumer, used to create its individual output file, e.g., `consumer_output_1.txt`.)*

    * **Terminal 4 (Run Consumer 2 - Optional):**
        ```bash
        # Replace <TOTAL_NUM_PRODUCERS> with the actual count (e.g., 2)
        ./bin/consumer <TOTAL_NUM_PRODUCERS> 2  # '2' is a unique ID for this consumer
        ```
        *(Run as many consumers as desired. Each must be given a unique ID.)*

    * **Wait for ALL Producers and ALL Consumers to Finish:**
        Observe the terminal output. Producers will announce "Shutting down..." once they finish their files. Consumers will print a summary and "Shutting down..." once they've processed all words and received sufficient EOF signals. **It is critical that all producer and consumer processes have naturally exited before proceeding to the next step.** 

    * **Terminal 5 (Run the Aggregator):**
        Once all producer and consumer processes are *completely done*:
        ```bash
        ./bin/aggregator
        ```
        This process will read all `consumer_output_*.txt` files, combine their word counts, sum up the totals for each unique word, sort them by frequency, and write the final comprehensive report to `aggregated_word_counts.txt`.

6.  **View the Final Aggregated Output:**
    ```bash
    cat aggregated_word_counts.txt
    ```

---

## Cleanup:

To remove compiled executables, all generated output files (`consumer_output_*.txt` and `aggregated_word_counts.txt`), and unlink any persistent IPC resources (shared memory segments and semaphores):

```bash
make clean