#include <stdio.h>
#include <stdlib.h>
#include "tsl.h"

#define NUM_THREADS 5

void print_numbers(void *arg) {
    int start = *((int *)arg);
    for (int i = start; i < start + 5; ++i) {
        printf("Thread %d: %d\n", tsl_gettid(), i);
        tsl_yield(TSL_ANY); // Yield execution to the next thread
    }
    tsl_exit(); // Terminate the current thread
}

int main() {
    // Initialize threading library with scheduling algorithm 2 (random)
    tsl_init(ALG_RANDOM);

    printf("Main Thread: Creating child threads.\n");

    int start_values[NUM_THREADS] = {1, 10, 20, 30, 40}; // Starting values for each thread
    int thread_ids[NUM_THREADS]; // Array to hold thread IDs

    // Create child threads in a loop
    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_ids[i] = tsl_create_thread(print_numbers, &start_values[i]);
        if (thread_ids[i] == TSL_ERROR) {
            printf("Failed to create thread %d\n", i);
            // Handle error
            exit(EXIT_FAILURE);
        }
    }

    // Simulate main thread work and yield to child threads
    for (int i = 0; i < 5; ++i) {
        printf("Main Thread: Running.\n");
        tsl_yield(TSL_ANY);
    }

    // Wait for each child thread to complete in a loop
    printf("Main Thread: Finished creating threads. Waiting for them to complete.\n");
    for (int i = 0; i < NUM_THREADS; ++i) {
        int result = tsl_join(thread_ids[i]);
        if (result == TSL_ERROR) {
            printf("Failed to join thread %d\n", i);
            // Handle error
            exit(EXIT_FAILURE);
        }
    }

    printf("Main Thread: All child threads have completed. Exiting.\n");

    tsl_exit();

    return 0;
}
