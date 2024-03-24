#include <stdio.h>
#include <stdlib.h>
#include "tsl.h"
int numValues = 0;
void count_nums(void *arg) {
    numValues++;
    tsl_exit(); // Terminate the current thread
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <num_threads>\n", argv[0]);
        return 1;
    }

    int num_threads = atoi(argv[1]);
    if (num_threads <= 0) {
        printf("Number of threads should be a positive integer\n");
        return 1;
    }

    // Initialize threading library with FCFS
    tsl_init(ALG_FCFS);

    printf("Main Thread: Creating %d child threads.\n", num_threads);

    // Create child threads
    for (int i = 0; i < num_threads; ++i) {
        int tid = tsl_create_thread(count_nums, NULL);
        if (tid == TSL_ERROR) {
            printf("Failed to create thread %d\n", i);
            // Handle error
            exit(EXIT_FAILURE);
        }
    }

    // Wait for each child thread to complete in a loop
    printf("Main Thread: Waiting for child threads to complete.\n");
    for (int i = 0; i < num_threads; ++i) {
        int result = tsl_join(i + 1); // Thread IDs start from 1
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
