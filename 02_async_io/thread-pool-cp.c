// No copyright. 2024, Vladislav Aleinik

#include "common.h"

// Aligned memory allocation:
#include <memory.h>

// CPU_SET macros:
#include <sched.h>
// Threads:
#include <pthread.h>

//===========================
// Copy procedure parameters
//===========================

#define NUM_THREADS             8U
#define NUM_HARDWARE_THREADS    1U
#define READ_BLOCK_SIZE         512U

//============================
// Thread function aprameters
//============================

typedef struct {
    size_t thread_i;
    uint8_t* buffer;
    size_t src_size;
    int src_fd;
    int dst_fd;
} THREAD_ARGS;

typedef struct {
    pthread_t tid;
} THREAD_INFO;

void* thread_func(void* thread_args)
{
    THREAD_ARGS* args = (THREAD_ARGS*) thread_args;

    //=====================
    // Actual file copying
    //=====================

    for (uint32_t i = 0U; i < args->src_size;)
    {
        size_t offset = i + args->thread_i * READ_BLOCK_SIZE;
        if (offset > args->src_size)
        {
            break;
        }

        ssize_t bytes_read = pread(args->src_fd, args->buffer, READ_BLOCK_SIZE, offset);
        if (bytes_read == -1)
        {
            fprintf(stderr, "Unable to read block [%x, %x)\n", i, i + READ_BLOCK_SIZE);
            exit(EXIT_FAILURE);
        }

        ssize_t bytes_written = pwrite(args->dst_fd, args->buffer, bytes_read, offset);
        if (bytes_written == -1 || bytes_written != bytes_read)
        {
            fprintf(stderr, "Unable to write block [%x, %lx)\n", i, i + bytes_read);
            exit(EXIT_FAILURE);
        }

        i += READ_BLOCK_SIZE * NUM_THREADS;
        if (bytes_read != READ_BLOCK_SIZE)
        {
            break;
        }
    }

    return NULL;
}

//=====================
// Main copy procedure
//=====================

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: thread-pool-cp <src> <dst>");
        exit(EXIT_FAILURE);
    }

    // Open source file and determine it's size:
    int src_fd;
    uint32_t src_size;
    open_src_file(argv[1], &src_fd, &src_size);

    // Create the destination file and allocate space on the disk:
    int dst_fd;
    open_dst_file(argv[2], &dst_fd, src_size);

    //===============================
    // Allocate intermediate buffers
    //===============================

    uint8_t* buffer = (uint8_t*) aligned_alloc(READ_BLOCK_SIZE, READ_BLOCK_SIZE * NUM_THREADS);
    if (buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate aligned buffer\n");
        exit(EXIT_FAILURE);
    }

    //====================
    // Create thread pool
    //====================

    // Initialize thread data:
    THREAD_ARGS args[NUM_THREADS];
    for (size_t i = 0U; i < NUM_THREADS; ++i)
    {
        args[i].thread_i = i;
        args[i].buffer   = &buffer[i * READ_BLOCK_SIZE];
        args[i].src_size = src_size;
        args[i].src_fd   = src_fd;
        args[i].dst_fd   = dst_fd;
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

    //============================
    // End of actual file copying
    //============================

    close_src_dst_files(argv[1], src_fd, src_size, argv[2], dst_fd);

    return EXIT_SUCCESS;
}
