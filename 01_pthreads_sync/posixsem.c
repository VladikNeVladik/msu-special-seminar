// No copyright. Vladislav Alenik, 2024

// Feature test macro:
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

// CPU_SET macros:
#include <sched.h>
// Threads:
#include <pthread.h>
// POSIX semaphores:
#include <sys/stat.h>
#include <semaphore.h>

//----------------------
// Benchmark parameters
//----------------------

#define NUM_THREADS 8U
#define NUM_HARDWARE_THREAD 8U

const size_t NUM_ITERATIONS = 10000000U;

//------------------
// Thread execution
//------------------

typedef struct {
    size_t thread_i;
    sem_t* sem;
} THREAD_ARGS;

// Variable to race on:
uint32_t var = 0U;

void* thread_func(void* thread_args)
{
    THREAD_ARGS* args = (THREAD_ARGS*) thread_args;

    printf("I am thread#%zu\n", args->thread_i);

    for (size_t i = 0U; i < NUM_ITERATIONS; ++i)
    {
        // Basic critical section among the threads:
        int ret = sem_wait(args->sem);
        if (ret == -1)
        {
            fprintf(stderr, "Unable to lock semaphore lock\n");
            exit(EXIT_FAILURE);
        }

        var++;

        ret = sem_post(args->sem);
        if (ret == -1)
        {
            fprintf(stderr, "Unable to unlock semaphore lock\n");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}

//------------------
// Thread benchmark
//------------------

typedef struct {
    pthread_t tid;
} THREAD_INFO;

const char* KEYSEED_FILE = "/var/tmp/shmem-sem-keyseed-file";

int main()
{
    // Initialize POSIX semaphore:
    sem_t sem;
    sem_init(&sem, 0 /* sem is not shared */, 1U /* init value */);

    // Initialize thread data:
    THREAD_ARGS args[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        args[i] = (THREAD_ARGS) {
            .thread_i = i,
            .sem      = &sem
        };
    }

    // Spawn threads:
    THREAD_INFO thread_info[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        // Initialize thread attributes:
        pthread_attr_t thread_attributes;
        int ret = pthread_attr_init(&thread_attributes);
        if (ret != 0)
        {
            fprintf(stderr, "Unable to call pthread_attr_init\n");
            exit(EXIT_FAILURE);
        }

        // Assign hardware thread to posix thread:
        cpu_set_t assigned_harts;
        CPU_ZERO(&assigned_harts);

        // Assumptions:
        // - There are NUM_HARDWARE_THREAD hardware threads.
        // - All harts from 0 to are present.
        size_t hart_i = i % NUM_HARDWARE_THREAD;
        CPU_SET(hart_i, &assigned_harts);

        // Set thread affinity:
        ret = pthread_attr_setaffinity_np(&thread_attributes, sizeof(cpu_set_t), &assigned_harts);
        if (ret != 0)
        {
            fprintf(stderr, "Unable to call pthread_attr_setaffinity_np\n");
            exit(EXIT_FAILURE);
        }

        // Create POSIX thread:
        ret = pthread_create(&thread_info[i].tid, &thread_attributes, thread_func, &args[i]);
        if (ret != 0)
        {
            fprintf(stderr, "Unable to create thread\n");
            exit(EXIT_FAILURE);
        }

        // Destroy thread attribute object:
        pthread_attr_destroy(&thread_attributes);
    }

    // Wait for all threads to finish execution:
    for (size_t i = 0; i < NUM_THREADS; ++i)
    {
        int ret = pthread_join(thread_info[i].tid, NULL);
        if (ret != 0)
        {
            fprintf(stderr, "Unable to join thread\n");
            exit(EXIT_FAILURE);
        }
    }

    // Print incremented variable:
    printf("Result of the computation: %u\n", var);

    return EXIT_SUCCESS;
}
