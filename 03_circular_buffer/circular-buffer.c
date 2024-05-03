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
// Atomics:
#include <stdatomic.h>

//======================
// Benchmark parameters
//======================

#define QUEUE_SIZE     8U
#define NUM_ITERATIONS 100000ULL

#define ENABLE_PADDING 0

#define ENABLE_SIMPLE  1

#define ENABLE_BACKOFF 0
#define NUM_RETRIES    10U

#define NUM_HARDWARE_THREADS 1U

//-------------------------
// Lock-free circular API
// NOTE: thx Evgeny Baskov
//-------------------------

#define CACHE_LINE_SIZE 256

typedef struct {
    uint64_t* data;
    uint32_t mask;

    uint32_t cached_head;
#if ENABLE_PADDING == 1
    uint8_t pad0[CACHE_LINE_SIZE];
#endif
    uint32_t cached_tail;
#if ENABLE_PADDING == 1
    uint8_t pad1[CACHE_LINE_SIZE];
#endif
    uint32_t head;
#if ENABLE_PADDING == 1
    uint8_t pad2[CACHE_LINE_SIZE];
#endif
    uint32_t tail;
} QUEUE;

void queue_init(QUEUE* queue, uint32_t size)
{
    if (size == 0 || ((size - 1) & size) != 0)
    {
        printf("queue_init: size (%u) is expected to be power of two\n", size);
        exit(EXIT_FAILURE);
    }

    queue->data = (uint64_t*) calloc(size, sizeof(uint64_t));
    if (queue->data == NULL)
    {
        printf("queue_init: size (%u) is too big\n", size);
        exit(EXIT_FAILURE);
    }

    queue->cached_head = 0U;
    queue->cached_tail = 0U;
    queue->head = 0U;
    queue->tail = 0U;
}

bool queue_enqueue(QUEUE* queue, uint64_t elem)
{
    uint32_t tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

    if ((int32_t)(tail - (queue->cached_head + queue->mask)) > 0)
    {
        uint32_t cached_head = atomic_load_explicit(&queue->head, memory_order_relaxed);

        queue->cached_head = cached_head;

        if ((int32_t)(tail - (queue->cached_head + queue->mask)) > 0)
        {
            return false;
        }
    }

    queue->data[tail & queue->mask] = elem;
    atomic_store_explicit(&queue->tail, tail + 1, memory_order_release);

    return true;
}

bool queue_dequeue(QUEUE* queue, uint64_t* elem)
{
    // Read value of buffer head:
    uint32_t head = atomic_load_explicit(&queue->head, memory_order_relaxed);

    if (queue->cached_tail == head)
    {
        uint32_t cached_tail = atomic_load_explicit(&queue->tail, memory_order_acquire);
        queue->cached_tail = cached_tail;

        if (cached_tail == head)
        {
            return false;
        }
    }

    *elem = queue->data[head & queue->mask];
    atomic_store_explicit(&queue->head, head + 1, memory_order_relaxed);

    return true;
}

bool queue_enqueue_simple(QUEUE* queue, uint64_t elem)
{
    uint32_t head = atomic_load_explicit(&queue->head, memory_order_relaxed);

    uint32_t tail = atomic_load_explicit(&queue->tail, memory_order_relaxed);

    if ((int32_t)(tail - (head + queue->mask)) > 0)
    {
        return false;
    }

    queue->data[tail & queue->mask] = elem; // (1)
    atomic_store_explicit(&queue->tail, tail + 1, memory_order_release); // (2)

    return true;
}

bool queue_dequeue_simple(QUEUE* queue, uint64_t* elem)
{
    uint32_t head = atomic_load_explicit(&queue->head, memory_order_relaxed);

    uint32_t tail = atomic_load_explicit(&queue->tail, memory_order_acquire); // (3)
    if (tail == head)
    {
        return false;
    }

    *elem = queue->data[head & queue->mask]; // (4)
    atomic_store_explicit(&queue->head, head + 1, memory_order_relaxed);

    return true;
}

//----------------
// Benchmark code
//----------------

void thread_producer(QUEUE* queue)
{
    for (uint64_t snd_i = 0U; snd_i < NUM_ITERATIONS; ++snd_i)
    {
        // Put element in the queue:
        bool success = queue_enqueue(queue, snd_i);
        for (uint32_t retry = 0U; !success; retry++)
        {
#if ENABLE_BACKOFF == 1
            if (retry == NUM_RETRIES)
            {
                retry = 0U;
                pthread_yield();
            }
#endif

#if ENABLE_SIMPLE == 1
            success = queue_enqueue_simple(queue, snd_i);
#else
            success = queue_enqueue(queue, snd_i);
#endif
        }
    }
}

void thread_consumer(QUEUE* queue)
{
    for (uint64_t rcv_i = 0U; rcv_i < NUM_ITERATIONS; ++rcv_i)
    {
        uint64_t snd_i = 0U;

        // Get element from the queue:
        bool success = false;
        for (uint32_t retry = 0U; !success; retry++)
        {
#if ENABLE_BACKOFF == 1
            if (retry == NUM_RETRIES)
            {
                retry = 0U;
                pthread_yield();
            }
#endif

#if ENABLE_SIMPLE == 1
            success = queue_dequeue_simple(queue, &snd_i);
#else
            success = queue_dequeue(queue, &snd_i);
#endif
        }

        // Compare result:
        if (snd_i != rcv_i)
        {
            printf("Invalid queue element: expected %lu, got %lu\n", rcv_i, snd_i);
            exit(EXIT_FAILURE);
        }
    }
}

//------------------
// Thread execution
//------------------

typedef struct {
    size_t thread_i;
    QUEUE* queue;
} THREAD_ARGS;

void* thread_func(void* thread_args)
{
    THREAD_ARGS* args = (THREAD_ARGS*) thread_args;

    if (args->thread_i == 0U)
    {
        thread_producer(args->queue);
    }
    else
    {
        thread_consumer(args->queue);
    }

    return NULL;
}

//------------------
// Thread benchmark
//------------------

#define NUM_THREADS 2U

typedef struct {
    pthread_t tid;
} THREAD_INFO;

int main()
{
    // Initialize queue:
    QUEUE queue;
    queue_init(&queue, QUEUE_SIZE);

    // Initialize thread data:
    THREAD_ARGS args[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        args[i].thread_i = i;
        args[i].queue = &queue;
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
        // - There are NUM_HARDWARE_THREADS hardware threads.
        // - All harts from 0 to are present.
        size_t hart_i = i % NUM_HARDWARE_THREADS;
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

    return EXIT_SUCCESS;
}
