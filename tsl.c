#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <unistd.h>

#define STACK_SIZE 1024*64
#define TSL_ANY 0
#define TSL_ERROR -1

typedef struct tsl_thread {
    int tid;
    ucontext_t context;
    struct tsl_thread *next;
    void (*start_routine)(void*);
    void *arg;
    int state; // 0: running, 1: ready, 2: terminated
} tsl_thread_t;

static tsl_thread_t *thread_list = NULL; // List of all threads
static tsl_thread_t *current_thread = NULL; // Currently running thread
static int next_tid = 1; // Next thread ID to assign
static ucontext_t main_context; // Main thread context

// Finds and removes a terminated thread from the list, freeing its resources
void cleanup_terminated_threads() {
    tsl_thread_t *prev = NULL, *thread = thread_list;
    while (thread) {
        if (thread->state == 2) { // If thread is terminated
            if (prev) {
                prev->next = thread->next;
            } else {
                thread_list = thread->next;
            }
            tsl_thread_t *temp = thread;
            thread = thread->next;
            free(temp->start_routine); // Assuming this was dynamically allocated
            continue;
        }
        prev = thread;
        thread = thread->next;
    }
}

void schedule() {
    tsl_thread_t *prev_thread = current_thread;

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
void tsl_exit() {
    current_thread->state = 2; // Mark as terminated
    schedule(); // Switch to the next thread
    // This function does not return as the current thread is terminated
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
    tsl_thread_t *thread = current_thread; // Assuming current_thread points to the newly created thread

    // Allocate a stack for the new thread
    void *stack = malloc(STACK_SIZE);
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
    thread->context.uc_stack.ss_size = STACK_SIZE;
    thread->context.uc_stack.ss_flags = 0;
    makecontext(&thread->context, (void (*)())thread_start_func, 1, info);
}


// Initialize the threading library
int tsl_init(int salg) {
    // Salg is not used in this simplified version, could be used to select scheduling algorithms
    return 0; // Success
}

// Create a new thread
int tsl_create_thread(void (*start_routine)(void *), void *arg) {
    tsl_thread_t *new_thread = (tsl_thread_t *)malloc(sizeof(tsl_thread_t));
    if (!new_thread) return TSL_ERROR;

    getcontext(&new_thread->context);
    new_thread->context.uc_link = 0;
    new_thread->context.uc_stack.ss_sp = malloc(STACK_SIZE);
    new_thread->context.uc_stack.ss_size = STACK_SIZE;
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

// Yield the CPU from the calling thread to another thread
int tsl_yield(int tid) {
    schedule(); // Switch to the next thread in round-robin order
    printf("Thread %d is running.\n", current_thread->tid);
    return current_thread ? current_thread->tid : TSL_ERROR;
}

int tsl_join(int tid) {
    tsl_thread_t *target = NULL;
    for (;;) {
        target = NULL;
        for (tsl_thread_t *thread = thread_list; thread != NULL; thread = thread->next) {
            if (thread->tid == tid) {
                target = thread;
                break;
            }
        }
        if (!target) return TSL_ERROR; // No thread with such tid
        if (target->state == 2) break; // Target thread has terminated
        tsl_yield(TSL_ANY); // Yield to allow other threads to run
    }
    // Cleanup
    free(target->context.uc_stack.ss_sp);
    free(target);
    return tid; // Return the tid of the terminated thread
}

int tsl_cancel(int tid) {
    for (tsl_thread_t **curr = &thread_list; *curr; curr = &(*curr)->next) {
        if ((*curr)->tid == tid) {
            tsl_thread_t *target = *curr;
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

void thread_start_stub() {
    current_thread->start_routine(current_thread->arg);
    tsl_exit();
}

void print_message(void *arg) {
    char *message = (char *)arg;
    for (int i = 0; i < 5; ++i) {
        printf("%s\n", message);
        sleep(1); // Simulate work
        tsl_yield(TSL_ANY); // Yield execution to the next thread
    }
    tsl_exit(); // Terminate the current thread
}

int main() {
    tsl_init(0); // Initialize threading library

    printf("Main Thread: Creating child threads.\n");

    tsl_create_thread(print_message, "Child Thread 1: Running.");
    tsl_create_thread(print_message, "Child Thread 2: Running.");
    tsl_create_thread(print_message, "Child Thread 3: Running.");

    // Simulate main thread work and yield to child threads
    for (int i = 0; i < 5; ++i) {
        printf("Main Thread: Running.\n");
        sleep(1); // Simulate work
        tsl_yield(TSL_ANY);
    }

    // Assuming we have a mechanism to wait for all threads to complete.
    // In a real implementation, you'd need something like tsl_join()
    // to ensure main doesn't exit early.
    printf("Main Thread: Finished creating threads. Waiting for them to complete.\n");
    while (1) {
        sleep(1); // In a real scenario, replace this with a proper wait/check mechanism.
    }

    return 0;
}
