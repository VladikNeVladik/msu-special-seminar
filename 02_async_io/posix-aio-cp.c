// No copyright. 2024, Vladislav Aleinik

#include "common.h"

#include <memory.h>
#include <aio.h>

//===========================
// Copy procedure parameters
//===========================

#define READ_BLOCK_SIZE 512U
#define QUEUE_SIZE 16U

//======================
// Basic AIO operations
//======================

void aio_read_setup(struct aiocb* aio, int fd, off_t offset, volatile void *buf, size_t size)
{
    // Remove info from previous request:
    memset(aio, 0, sizeof(struct aiocb));

    aio->aio_fildes = fd;       // File descriptor for the file to read from.
    aio->aio_buf    = buf;      // Buffer to read the data into.
    aio->aio_nbytes = size;     // Number of bytes to read.
    aio->aio_offset = offset;   // Offset in the file to start reading from.

    // Initiate the read operation:
    if (aio_read(aio) == -1)
    {
        perror("Unable to request read");
        exit(EXIT_FAILURE);
    }
}

void aio_write_setup(struct aiocb* aio, int fd, off_t offset, volatile void *buf, size_t size)
{
    // Remove info from previous request:
    memset(aio, 0, sizeof(struct aiocb));

    aio->aio_fildes = fd;       // File descriptor for the file to write to.
    aio->aio_buf    = buf;      // Buffer to write the data from.
    aio->aio_nbytes = size;     // Number of bytes to written.
    aio->aio_offset = offset;   // Offset in the file to start writing from.

    // Initiate the qrite operation:
    if (aio_write(aio) == -1)
    {
        perror("Unable to request write");
        exit(EXIT_FAILURE);
    }
}

//=====================
// Main copy procedure
//=====================

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "Usage: sync-cp <src> <dst>");
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

    uint8_t* buffer = (uint8_t*) aligned_alloc(READ_BLOCK_SIZE, READ_BLOCK_SIZE * QUEUE_SIZE);
    if (buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate aligned buffer\n");
        exit(EXIT_FAILURE);
    }

    //==============================
    // Prepare AIO meta-information
    //==============================

    // Array of AIO control blocks:
    struct aiocb* aiocbs = calloc(QUEUE_SIZE, sizeof(struct aiocb));
    if (aiocbs == NULL)
    {
        fprintf(stderr, "Unable to allocate AIO control blocks\n");
        exit(EXIT_FAILURE);
    }

    // Array of ongoing AIO requests:
    struct aiocb* wait_list[QUEUE_SIZE];
    for (size_t aio_i = 0U; aio_i < QUEUE_SIZE; ++aio_i)
    {
        wait_list[aio_i] = NULL;
    }

    //=====================
    // Actual file copying
    //=====================

    // Round file size:
    src_size = src_size + READ_BLOCK_SIZE - (src_size % READ_BLOCK_SIZE);

    // Start initial read requests:
    off_t src_off = 0U;
    size_t num_io_reqs = 0U;
    for (size_t aio_i = 0U; aio_i < QUEUE_SIZE && src_off < src_size; ++aio_i, ++num_io_reqs)
    {
        aio_read_setup(&aiocbs[aio_i], src_fd, src_off,
            &buffer[aio_i * READ_BLOCK_SIZE], READ_BLOCK_SIZE);

        // Put AIO in wait list:
        wait_list[aio_i] = &aiocbs[aio_i];

        src_off += READ_BLOCK_SIZE;
    }

    // Cycle while there are active I/Os
    while (num_io_reqs != 0U)
    {
        int suspend_ret = aio_suspend((const struct aiocb * const*) wait_list, QUEUE_SIZE, NULL);
        if (suspend_ret == -1)
        {
            printf("Unable to suspend-wait for AIOs\n");
            exit(EXIT_FAILURE);
        }

        for (size_t aio_i = 0U; aio_i < QUEUE_SIZE; ++aio_i)
        {
            // Skip if AIO is already done:
            if (wait_list[aio_i] == NULL) continue;

            // Skip if AIO is still in progress:
            int error_ret = aio_error(&aiocbs[aio_i]);
            if (error_ret == EINPROGRESS) continue;

            if (aiocbs[aio_i].aio_lio_opcode == LIO_READ)
            {
                int bytes_read = aio_return(&aiocbs[aio_i]);
                if (bytes_read != 0)
                {
                    // Now write read data:
                    aio_write_setup(&aiocbs[aio_i], dst_fd, aiocbs[aio_i].aio_offset,
                        &buffer[aio_i * READ_BLOCK_SIZE], bytes_read);
                }
                else
                {
                    // Remove AIO from wait list:
                    wait_list[aio_i] = NULL;

                    num_io_reqs -= 1U;
                }

            }
            else if (aiocbs[aio_i].aio_lio_opcode == LIO_WRITE)
            {
                int bytes_written = aio_return(&aiocbs[aio_i]);
                if (bytes_written != 0 && src_off < src_size)
                {
                    // Request another read operation:
                    aio_read_setup(&aiocbs[aio_i], src_fd, src_off,
                        &buffer[aio_i * READ_BLOCK_SIZE], READ_BLOCK_SIZE);

                    src_off += READ_BLOCK_SIZE;
                }
                else
                {
                    // Remove AIO from wait list:
                    wait_list[aio_i] = NULL;

                    num_io_reqs -= 1U;
                }
            }
        }
    }

    //============================
    // End of actual file copying
    //============================

    close_src_dst_files(argv[1], src_fd, src_size, argv[2], dst_fd);

    return EXIT_SUCCESS;
}
