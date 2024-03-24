#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include "tsl.h"

typedef struct TCB {
    int tid;
    ucontext_t context;
    struct TCB *next;
    void (*start_routine)(void*);
    void *arg;
    int state; // 0: running, 1: ready, 2: terminated
    char *stack;
} TCB;

static TCB *thread_list = NULL; // List of all threads
static TCB *current_thread = NULL; // Currently running thread
static int next_tid = 1; // Next thread ID to assign
static ucontext_t main_context; // Main thread context
int currSchedAlgo = 1;

int initCalled = -1;

// Finds and removes a terminated thread from the list, freeing its resources
void cleanup_terminated_threads() {
    TCB *prev = NULL, *thread = thread_list;
    while (thread) {
        if (thread->state == 2) { // If thread is terminated
            if (prev) {
                prev->next = thread->next;
            } else {
                thread_list = thread->next;
            }
            TCB *temp = thread;
            thread = thread->next;
            free(temp->start_routine); // Assuming this was dynamically allocated
            continue;
        }
        prev = thread;
        thread = thread->next;
    }
}

void schedule_random() {
    TCB *prev_thread = current_thread;

    // Attempt to save the current context and switch to the main context
    if (prev_thread && swapcontext(&prev_thread->context, &main_context) == -1) {
        perror("swapcontext failed");
        exit(EXIT_FAILURE);
    }

    // Cleanup terminated threads and free their resources
    cleanup_terminated_threads();

    // If the current thread is not set or it's the last in the list, start from the beginning
    if (!current_thread || !current_thread->next) {
        current_thread = thread_list;
    } else {
        // Otherwise, move to the next thread in the list
        current_thread = current_thread->next;
    }
     
    int num = (rand() % (TSL_MAXTHREADS - 2 + 1)) + 2; //we dont want to select main thread which is in the index 0
    

    // Find the next thread that is ready to run
    while (num != current_thread->tid && current_thread && current_thread->state != 1) { // Skip terminated threads
        current_thread = current_thread->next ? current_thread->next : thread_list; // Wrap around if at the end
    }

    // If a runnable thread is found, switch to its context
    if (current_thread) {
        if (setcontext(&current_thread->context) == -1) {
            perror("setcontext failed");
            exit(EXIT_FAILURE);
        }
    }
}

void schedule() {
    TCB *prev_thread = current_thread;

    // Attempt to save the current context and switch to the main context
    if (prev_thread && swapcontext(&prev_thread->context, &main_context) == -1) {
        perror("swapcontext failed");
        exit(EXIT_FAILURE);
    }

    // Cleanup terminated threads and free their resources
    cleanup_terminated_threads();

    // If the current thread is not set or it's the last in the list, start from the beginning
    if (!current_thread || !current_thread->next) {
        current_thread = thread_list;
    } else {
        // Otherwise, move to the next thread in the list
        current_thread = current_thread->next;
    }

    // Find the next thread that is ready to run
    while (current_thread && current_thread->state != 1) { // Skip terminated threads
        current_thread = current_thread->next ? current_thread->next : thread_list; // Wrap around if at the end
    }

    // If a runnable thread is found, switch to its context
    if (current_thread) {
        if (setcontext(&current_thread->context) == -1) {
            perror("setcontext failed");
            exit(EXIT_FAILURE);
        }
    }
}

// Current thread calls exit
int tsl_exit() {
    if(initCalled == -1){
        printf("error: tsl_init() not called\n");
        exit(1);
    }

    current_thread->state = 2; // Mark as terminated
    if(currSchedAlgo == 1){
            schedule();      // Switch to the next thread
        }
        else if(currSchedAlgo == 2){
            schedule_random();
        }
        else{
            return TSL_ERROR;
        }
    
    // This function does not return as the current thread is terminated
    //return 0;
}

typedef void (*thread_start_routine)(void *);

// Struct to hold start routine and argument
typedef struct thread_start_info {
    thread_start_routine start_routine;
    void *arg;
} thread_start_info_t;

// Actual start routine that gets called by makecontext
void thread_start_func(thread_start_info_t *info) {
    // Call the provided start routine with the provided argument
    info->start_routine(info->arg);
    
    // Automatically call tsl_exit when the start routine returns
    tsl_exit();
}

// Wrapper function to set up the thread's context and stack
void thread_start(void (*start_routine)(void *), void *arg) {
    TCB *thread = current_thread; // Assuming current_thread points to the newly created thread

    // Allocate a stack for the new thread
    void *stack = malloc(TSL_STACKSIZE);
    if (!stack) {
        perror("Failed to allocate stack");
        exit(1);
    }

    // Allocate memory for thread_start_info
    thread_start_info_t *info = malloc(sizeof(thread_start_info_t));
    if (!info) {
        perror("Failed to allocate thread start info");
        exit(1);
    }
    info->start_routine = start_routine;
    info->arg = arg;

    // Initialize the thread's context to call thread_start_func
    getcontext(&thread->context);
    thread->context.uc_link = 0;
    thread->context.uc_stack.ss_sp = stack;
    thread->context.uc_stack.ss_size = TSL_STACKSIZE;
    thread->context.uc_stack.ss_flags = 0;
    makecontext(&thread->context, (void (*)())thread_start_func, 1, info);
}


// Initialize the threading library
//int tsl_init(int salg) {
//    // Salg is not used in this simplified version, could be used to select scheduling algorithms
//    return 0; // Success
//}

// Initialize the threading library
int tsl_init(int salg) {
    initCalled = 1;
    currSchedAlgo = salg;
    srand(time(0));
    // Salg is not used in this simplified version, could be used to select scheduling algorithms
    // For now, we just initialize the main thread
    TCB *main_thread = (TCB *)malloc(sizeof(TCB));
    if (!main_thread) return TSL_ERROR;

    // Initialize main thread context
    getcontext(&main_thread->context);
    main_thread->context.uc_link = 0;
    main_thread->context.uc_stack.ss_sp = malloc(TSL_STACKSIZE);
    main_thread->context.uc_stack.ss_size = TSL_STACKSIZE;
    main_thread->context.uc_stack.ss_flags = 0;
    if (!main_thread->context.uc_stack.ss_sp) {
        free(main_thread);
        return TSL_ERROR;
    }

    main_thread->tid = 0; // Main thread ID is 0
    main_thread->start_routine = NULL; // Main thread doesn't have a start routine
    main_thread->arg = NULL; // Main thread doesn't have arguments
    main_thread->state = 0; // Main thread is running
    main_thread->next = NULL;

    current_thread = main_thread; // Set current thread to main thread

    // Save main thread context for later use
    if (getcontext(&main_context) == -1) {
        perror("getcontext failed");
        exit(EXIT_FAILURE);
    }
    main_context.uc_link = 0;
    main_context.uc_stack.ss_sp = malloc(TSL_STACKSIZE);
    main_context.uc_stack.ss_size = TSL_STACKSIZE;
    main_context.uc_stack.ss_flags = 0;
    if (!main_context.uc_stack.ss_sp) {
        free(main_thread->context.uc_stack.ss_sp);
        free(main_thread);
        return TSL_ERROR;
    }
    main_context.uc_stack.ss_sp = main_thread->context.uc_stack.ss_sp;
    main_context.uc_stack.ss_size = main_thread->context.uc_stack.ss_size;

    return 0; // Success
}


// Create a new thread
int tsl_create_thread(void (*start_routine)(void *), void *arg) {
    if(initCalled == -1){
        printf("error: tsl_init() not called\n");
        exit(1);
    }

    TCB *new_thread = (TCB *)malloc(sizeof(TCB));
    if (!new_thread) return TSL_ERROR;

    getcontext(&new_thread->context);
    new_thread->context.uc_link = 0;
    new_thread->context.uc_stack.ss_sp = malloc(TSL_STACKSIZE);
    new_thread->context.uc_stack.ss_size = TSL_STACKSIZE;
    new_thread->context.uc_stack.ss_flags = 0;
    if (new_thread->context.uc_stack.ss_sp == 0) {
        free(new_thread);
        return TSL_ERROR;
    }

    makecontext(&new_thread->context, (void (*)())thread_start, 2, start_routine, arg);

    new_thread->tid = next_tid++;
    new_thread->start_routine = start_routine;
    new_thread->arg = arg;
    new_thread->state = 1; // Ready
    new_thread->next = thread_list;
    thread_list = new_thread;

    return new_thread->tid; // Return the new thread ID
}

// // Yield the CPU from the calling thread to another thread
// int tsl_yield(int tid) {
//     schedule(); // Switch to the next thread in round-robin order
//     printf("Thread %d is running.\n", current_thread->tid);
//     return current_thread ? current_thread->tid : TSL_ERROR;
// }
// Yield the CPU from the calling thread to another thread
int tsl_yield(int tid) {
    TCB *prev_thread = current_thread;

    // If tid is TSL_ANY or tid is the same as the current thread, just schedule the next thread
    if (tid == TSL_ANY || (current_thread && current_thread->tid == tid)) {
        if(currSchedAlgo == 1){
            schedule();
        }
        else if(currSchedAlgo == 2){
            schedule_random();
        }
        else{
            return TSL_ERROR;
        }
        return current_thread ? current_thread->tid : TSL_ERROR;
    }

    // Search for the thread with the specified tid
    TCB *target_thread = NULL;
    for (TCB *thread = thread_list; thread != NULL; thread = thread->next) {
        if (thread->tid == tid) {
            target_thread = thread;
            break;
        }
    }

    // If the target thread is found and it's in the ready state, perform context switch
    if (target_thread && target_thread->state == 1) {
        current_thread = target_thread;
        if (swapcontext(&prev_thread->context, &current_thread->context) == -1) {
            perror("swapcontext failed");
            exit(EXIT_FAILURE);
        }
        return current_thread->tid;
    }

    // If tid is a positive integer but there is no ready thread with that tid, return error
    return TSL_ERROR;
}


// int tsl_join(int tid) {
//     TCB *target = NULL;
//     for (;;) {
//         target = NULL;
//         for (TCB *thread = thread_list; thread != NULL; thread = thread->next) {
//             if (thread->tid == tid) {
//                 target = thread;
//                 break;
//             }
//         }
//         if (!target) return TSL_ERROR; // No thread with such tid
//         if (target->state == 2) break; // Target thread has terminated
//         tsl_yield(TSL_ANY); // Yield to allow other threads to run
//     }
//     // Cleanup
//     free(target->context.uc_stack.ss_sp);
//     free(target);
//     return tid; // Return the tid of the terminated thread
// }

int tsl_join(int tid) {
    if(initCalled == -1){
        printf("error: tsl_init() not called\n");
        exit(1);
    }
    // Find the target thread
    TCB *target_thread = NULL;
    for (TCB *thread = thread_list; thread != NULL; thread = thread->next) {
        if (thread->tid == tid) {
            target_thread = thread;
            break;
        }
    }

    // If the target thread doesn't exist or has already terminated, return error
    if (!target_thread || target_thread->state == 2) {
        return TSL_ERROR;
    }

    // Wait for the target thread to terminate
    while (target_thread->state != 2) {
        tsl_yield(TSL_ANY); // Yield to allow other threads to run
    }

    // Cleanup resources
    free(target_thread->context.uc_stack.ss_sp);
    free(target_thread);

    return tid; // Return the tid of the terminated thread
}


int tsl_cancel(int tid) {
    for (TCB **curr = &thread_list; *curr; curr = &(*curr)->next) {
        if ((*curr)->tid == tid) {
            TCB *target = *curr;
            *curr = target->next; // Remove from the list

            // Cleanup
            if (target->context.uc_stack.ss_sp != NULL) {
                free(target->context.uc_stack.ss_sp);
            }
            free(target);

            return 0; // Success
        }
    }
    return TSL_ERROR; // No thread with such tid
}

int tsl_gettid() {
    return current_thread ? current_thread->tid : TSL_ERROR;
}

void stub() {
    current_thread->start_routine(current_thread->arg);
    tsl_exit();
}

// void print_message(void *arg) {
//     char *message = (char *)arg;
//     for (int i = 0; i < 5; ++i) {
//         printf("%s\n", message);
//         //sleep(10); // Simulate work
//         tsl_yield(TSL_ANY); // Yield execution to the next thread
//     }
//     tsl_exit(); // Terminate the current thread
// }

