// No copyright. 2024, Vladislav Aleinik

#include "common.h"

#include <memory.h>
#include <libaio.h>

//===========================
// Copy procedure parameters
//===========================

#define READ_BLOCK_SIZE 8192U
#define QUEUE_SIZE 64U

//======================
// Basic AIO operations
//======================

void io_read_setup(struct iocb* aio, int fd, off_t offset, void *buf, size_t size)
{
    // Remove info from previous request:
    memset(aio, 0, sizeof(struct iocb));

    aio->aio_fildes     = fd;           // File descriptor for the file to read from.
    aio->aio_lio_opcode = IO_CMD_PREAD; // Command
    aio->aio_reqprio    = 0;            // Request priority
    aio->u.c.buf        = buf;          // Buffer to read the data into.
    aio->u.c.nbytes     = size;         // Number of bytes to read.
    aio->u.c.offset     = offset;       // Offset in the file to start reading from.
}

void io_write_setup(struct iocb* aio, int fd, off_t offset, void *buf, size_t size)
{
    // Remove info from previous request:
    memset(aio, 0, sizeof(struct iocb));

    aio->aio_fildes     = fd;            // File descriptor for the file to read from.
    aio->aio_lio_opcode = IO_CMD_PWRITE; // Command
    aio->aio_reqprio    = 0;             // Request priority
    aio->u.c.buf        = buf;           // Buffer to read the data into.
    aio->u.c.nbytes     = size;          // Number of bytes to read.
    aio->u.c.offset     = offset;        // Offset in the file to start reading from.
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

    // AIO context:
    io_context_t io_ctx;
    memset(&io_ctx, 0, sizeof(io_ctx));

    int setup_ret = io_setup(QUEUE_SIZE, &io_ctx);
    if (setup_ret != 0)
    {
        fprintf(stderr, "Unable to setup AIO context\n");
        exit(EXIT_FAILURE);
    }

    // IO buffers:
    struct iocb iocbs[QUEUE_SIZE];

    // IO events:
    struct io_event events[QUEUE_SIZE];

    // Array of ongoing AIO requests:
    struct iocb* submit_list[QUEUE_SIZE];
    for (size_t aio_i = 0U; aio_i < QUEUE_SIZE; ++aio_i)
    {
        submit_list[aio_i] = NULL;
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
        io_read_setup(&iocbs[aio_i], src_fd, src_off,
            &buffer[aio_i * READ_BLOCK_SIZE], READ_BLOCK_SIZE);

        // Put I/O in submit list:
        submit_list[aio_i] = &iocbs[aio_i];

        src_off += READ_BLOCK_SIZE;
    }

    // Cycle while there are active I/Os
    size_t num_to_submit = num_io_reqs;
    while (num_io_reqs != 0U)
    {
        // Submit all I/Os:
        int submit_ret = io_submit(io_ctx, num_to_submit, submit_list);
        if (submit_ret < 0)
        {
            fprintf(stderr, "Unable to submit I/Os\n");
            exit(EXIT_FAILURE);
        }

        // Wait for at least one I/O:
        int num_events = io_getevents(io_ctx, 1U, QUEUE_SIZE, events, NULL);
        if (num_events < 0)
        {
            printf("Unable to get finished I/O events\n");
            exit(EXIT_FAILURE);
        }

        // Handle finished requests:
        num_to_submit = 0U;
        for (int ev = 0U; ev < num_events; ++ev)
        {
            // Get current iocb:
            struct iocb* iocb = events[ev].obj;
            int io_ret        = events[ev].res;

            if (iocb->aio_lio_opcode == IO_CMD_PREAD)
            {
                int bytes_read = io_ret;
                if (bytes_read != 0)
                {
                    // Now write read data:
                    io_write_setup(iocb, dst_fd, iocb->u.c.offset, iocb->u.c.buf, bytes_read);

                    // Register request into submit list:
                    submit_list[num_to_submit] = iocb;
                    num_to_submit++;
                }
                else
                {
                    num_io_reqs -= 1U;
                }

            }
            else if (iocb->aio_lio_opcode == IO_CMD_PWRITE)
            {
                int bytes_written = io_ret;
                if (bytes_written != 0 && src_off < src_size)
                {
                    // Request another read operation:
                    io_read_setup(iocb, src_fd, src_off, iocb->u.c.buf, READ_BLOCK_SIZE);

                    // Register request into submit list:
                    submit_list[num_to_submit] = iocb;
                    num_to_submit++;

                    src_off += READ_BLOCK_SIZE;
                }
                else
                {
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
