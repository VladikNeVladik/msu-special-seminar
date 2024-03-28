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
// Atomic operations:
#include <stdatomic.h>

//----------------------
// Benchmark parameters
//----------------------

#define NUM_THREADS 8U
#define NUM_HARDWARE_THREAD 8U

const size_t NUM_ITERATIONS = 10000000U;

//-------------------------
// Spinlock implementation
//-------------------------

typedef struct
{
    atomic_flag lock_taken;
} TAS_Lock;

void TAS_init(TAS_Lock* lock)
{
    atomic_flag_clear_explicit(&lock->lock_taken, memory_order_release);
}

void TAS_acquire(TAS_Lock* lock)
{
    while (atomic_flag_test_and_set_explicit(&lock->lock_taken, memory_order_acquire) != 0)
    {
        // ASM instruction to ask processor cool down.
        __asm__ volatile("pause");
    }
}

void TAS_release(TAS_Lock* lock)
{
    atomic_flag_clear_explicit(&lock->lock_taken, memory_order_release);
}

//------------------
// Thread execution
//------------------

typedef struct {
    size_t thread_i;
    TAS_Lock* spinlock;
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
        TAS_acquire(args->spinlock);

        var++;

        TAS_release(args->spinlock);
    }

    return NULL;
}

//------------------
// Thread benchmark
//------------------

typedef struct {
    pthread_t tid;
} THREAD_INFO;


int main()
{
    // Initialize mutual exclusion object:
    TAS_Lock spinlock;
    TAS_init(&spinlock);

    // Initialize thread data:
    THREAD_ARGS args[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        args[i].thread_i = i;
        args[i].spinlock = &spinlock;
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
