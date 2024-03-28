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
// SYS V semaphores:
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

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
    int semset_id;
    struct sembuf* sem_ops_lock;
    size_t num_ops_lock;
    struct sembuf* sem_ops_unlock;
    size_t num_ops_unlock;
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
        int ret = semop(args->semset_id, args->sem_ops_lock, args->num_ops_lock);
        if (ret == -1)
        {
            fprintf(stderr, "Unable to lock SYS V semaphore\n");
            exit(EXIT_FAILURE);
        }

        var++;

        ret = semop(args->semset_id, args->sem_ops_unlock, args->num_ops_unlock);
        if (ret == -1)
        {
            fprintf(stderr, "Unable to unlock SYS V semaphore\n");
            exit(EXIT_FAILURE);
        }
    }

    return NULL;
}

//------------------
// Thread benchmark
//------------------

// man 2 semctl requires user to define a wierd union :(
typedef union {
   int              val;    /* Value for SETVAL */
   struct semid_ds *buf;    /* Buffer for IPC_STAT, IPC_SET */
   unsigned short  *array;  /* Array for GETALL, SETALL */
   struct seminfo  *__buf;  /* Buffer for IPC_INFO (Linux-specific) */
} SEM_UNION;

typedef struct {
    pthread_t tid;
} THREAD_INFO;

const char* KEYSEED_FILE = "/var/tmp/shmem-sem-keyseed-file";

int main()
{
    // Initialize SYS V semaphore:
    int semset_key = ftok(KEYSEED_FILE, 0);
    if (semset_key == -1)
    {
        perror("Unable to get semaphore key out of the keyseed file\n");
        exit(EXIT_FAILURE);
    }

    // Get semaphore set:
    int semset_id = semget(semset_key, 1, IPC_CREAT|0600);
    if (semset_id == -1)
    {
        fprintf(stderr, "Unable to allocate SYS V semaphore object\n");
        exit(EXIT_FAILURE);
    }

    // Pre-initialize semaphore opreations:
    struct sembuf sem_ops_lock[2] = {
    {
        .sem_num = 0, // Operate on sem#0
        .sem_op  = 0, // Wait for sem value to be 0
        .sem_flg = 0  // No flags
    },
    {
        .sem_num = 0,       // Operate on sem#0
        .sem_op  = +1,      // Increase value to one
        .sem_flg = SEM_UNDO // No flags
    }};

    struct sembuf sem_ops_unlock[1] = {
    {
        .sem_num =  0, // Operate on sem#0
        .sem_op  = -1, // Decrease sem value by 1
        .sem_flg =  0  // No flags
    }};

    // Initialize thread data:
    THREAD_ARGS args[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        args[i] = (THREAD_ARGS) {
            .thread_i       = i,
            .semset_id      = semset_id,
            .sem_ops_lock   = sem_ops_lock,
            .num_ops_lock   = 2U,
            .sem_ops_unlock = sem_ops_unlock,
            .num_ops_unlock = 1U
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
