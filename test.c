#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "tsl.h"

void print_message(void *arg) {
    char *message = (char *)arg;
    for (int i = 0; i < 5; ++i) {
        printf("%s\n", message);
        //sleep(10); // Simulate work
        tsl_yield(TSL_ANY); // Yield execution to the next thread
    }
    tsl_exit(); // Terminate the current thread
}

int main() {
    tsl_init(ALG_FCFS); // Initialize threading library

    printf("Main Thread: Creating child threads.\n");

    // Create child threads
    int tid1 = tsl_create_thread(print_message, "Child Thread 1: Running.");
    int tid2 = tsl_create_thread(print_message, "Child Thread 2: Running.");
    int tid3 = tsl_create_thread(print_message, "Child Thread 3: Running.");

    // Simulate main thread work and yield to child threads
    for (int i = 0; i < 5; ++i) {
        printf("Main Thread: Running.\n");
        //sleep(1); // Simulate work
        tsl_yield(TSL_ANY);
    }

    // Wait for each child thread to complete
    printf("Main Thread: Finished creating threads. Waiting for them to complete.\n");
    tsl_join(tid1);
    tsl_join(tid2);
    tsl_join(tid3);

    printf("Main Thread: All child threads have completed. Exiting.\n");

    tsl_exit();

    return 0;
}