// No copyright. 2024, Vladislav Aleinik

#include "common.h"

#include <memory.h>
#include <liburing.h>

//===========================
// Copy procedure parameters
//===========================

#define READ_BLOCK_SIZE 8192U
#define QUEUE_SIZE 64U

//================
// Copying status
//================

typedef enum {
    BLOCK_IDLE     = 0,
    BLOCK_IN_READ  = 1,
    BLOCK_IN_WRITE = 2
} BlockStage;

struct BlockStatus
{
    BlockStage stage;

    off64_t offset;
    uint32_t size;
};

struct CopyStatus
{
    int src_fd;
    int dst_fd;

    off_t src_off;
    uint32_t src_size;

    uint16_t num_block_in_progress;

    struct BlockStatus block_statuses[QUEUE_SIZE];

    char* aligned_buffers;
    struct iovec* fixed_buffers;

    struct io_uring io_ring;
};

void init_copying_status(struct CopyStatus* status, uint32_t src_size, int src_fd, int dst_fd)
{
    status->src_fd   = src_fd;
    status->dst_fd   = dst_fd;
    status->src_off  = 0;
    status->src_size = src_size;

    status->num_block_in_progress = 0;

    for (uint16_t i = 0; i < QUEUE_SIZE; ++i)
    {
        status->block_statuses[i].stage  = BLOCK_IDLE;
        status->block_statuses[i].offset = 0;
        status->block_statuses[i].size   = 0;
    }

    // Initialize IO-userspace-ring:
    int init_ret = io_uring_queue_init(QUEUE_SIZE, &status->io_ring, 0U);
    if (init_ret != 0)
    {
        printf("Unable to initialize IO-ring: errno=%i (%s)", init_ret, strerror(init_ret));
        exit(EXIT_FAILURE);
    }

    // Create buffers to store intermediate data:
    status->aligned_buffers = (char*) aligned_alloc(READ_BLOCK_SIZE, QUEUE_SIZE * READ_BLOCK_SIZE);

    status->fixed_buffers = calloc(QUEUE_SIZE, sizeof(struct iovec));

    for (unsigned i = 0; i < QUEUE_SIZE; ++i)
    {
        status->fixed_buffers[i].iov_base = status->aligned_buffers + i * READ_BLOCK_SIZE;
        status->fixed_buffers[i].iov_len  = READ_BLOCK_SIZE;
    }

    if (io_uring_register_buffers(&status->io_ring, status->fixed_buffers, QUEUE_SIZE) != 0)
    {
        printf("Unable to register intermediate buffers: errno=%i (%s)", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void free_copying_status(struct CopyStatus* status)
{
    free(status->aligned_buffers);
    free(status->fixed_buffers);
}

//=====================
// Basic IO operations
//=====================

void prepare_read_request(struct CopyStatus* status, unsigned cell)
{
    struct BlockStatus* block = &status->block_statuses[cell];

    uint32_t bytes_left = status->src_size - status->src_off;
    if (bytes_left == 0)
    {
        return;
    }

    // Get the block to transfer:
    block->stage  = BLOCK_IN_READ;
    block->offset = status->src_off;
    block->size   = (bytes_left < READ_BLOCK_SIZE)? bytes_left : READ_BLOCK_SIZE;

    // Enqueue read request:
    struct io_uring_sqe* read_sqe = io_uring_get_sqe(&status->io_ring);

    io_uring_prep_read_fixed(read_sqe, status->src_fd,
                             status->fixed_buffers[cell].iov_base,
                             READ_BLOCK_SIZE, block->offset, cell);

    read_sqe->user_data = cell;

    // Update transfer status:
    status->src_off += block->size;
    status->num_block_in_progress += 1;

    // printf("Cell#%02d:  read (off=%lu, size=%u)\n", cell, block->offset, block->size);
}

void prepare_write_request(struct CopyStatus* status, unsigned cell)
{
    struct BlockStatus* block = &status->block_statuses[cell];

    block->stage = BLOCK_IN_WRITE;

    // Enqueue write request:
    struct io_uring_sqe* write_sqe = io_uring_get_sqe(&status->io_ring);

    io_uring_prep_write_fixed(write_sqe, status->dst_fd,
                              status->fixed_buffers[cell].iov_base,
                              READ_BLOCK_SIZE, block->offset, cell);

    // Update transfer status:
    write_sqe->user_data = cell;

    // printf("Cell#%02d: write (off=%lu, size=%u)\n", cell, block->offset, block->size);
}

void finish_write_request(struct CopyStatus* status, unsigned cell)
{
    struct BlockStatus* block = &status->block_statuses[cell];

    block->stage = BLOCK_IDLE;

    // Update transfer status:
    status->num_block_in_progress -= 1;

    // printf("Cell#%02d is IDLE\n", cell);
}

//=====================
// Main copy procedure
//=====================

#define MAX(a, b) ((a) > (b)? (a) : (b))

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

    struct CopyStatus status;
    init_copying_status(&status, src_size, src_fd, dst_fd);

    //=====================
    // Actual file copying
    //=====================

    // Use all idle cells for reads:
    for (uint32_t cell_i = 0; cell_i < QUEUE_SIZE; ++cell_i)
    {
        prepare_read_request(&status, cell_i);
    }

    while (status.src_off != status.src_size || status.num_block_in_progress != 0)
    {
        // Submit all unsubmitted reqs:
        io_uring_submit_and_wait(&status.io_ring, 1U);

        int64_t cell_i = -1;
        do
        {
            struct io_uring_cqe* done_req;

            int ret = io_uring_peek_cqe(&status.io_ring, &done_req);
            if (ret == 0) cell_i = done_req->user_data;
            else          cell_i = -1;

            if (cell_i != -1 && status.block_statuses[cell_i].stage == BLOCK_IN_READ)
            {
                if (done_req->res < 0)
                {
                    printf("Read operation failed at offset: %lu", status.block_statuses[cell_i].offset);
                    exit(EXIT_FAILURE);
                }

                prepare_write_request(&status, cell_i);
            }
            else if (cell_i != -1 && status.block_statuses[cell_i].stage == BLOCK_IN_WRITE)
            {
                if (done_req->res < 0)
                {
                    printf("Write operation failed at offset: %lu", status.block_statuses[cell_i].offset);
                    exit(EXIT_FAILURE);
                }

                finish_write_request(&status, cell_i);
                prepare_read_request(&status, cell_i);
            }

            io_uring_cqe_seen(&status.io_ring, done_req);
        }
        while (cell_i != -1);
    }

    // Deallocate resources:
    io_uring_queue_exit(&status.io_ring);

    free_copying_status(&status);

    //============================
    // End of actual file copying
    //============================

    close_src_dst_files(argv[1], src_fd, src_size, argv[2], dst_fd);

    return EXIT_SUCCESS;
}
