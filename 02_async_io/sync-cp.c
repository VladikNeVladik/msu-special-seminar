// No copyright. 2024, Vladislav Aleinik

#include "common.h"

#include <memory.h>

//===========================
// Copy procedure parameters
//===========================

#define READ_BLOCK_SIZE 512U

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

    //==============================
    // Allocate intermediate buffer
    //==============================

    uint8_t* buffer = (uint8_t*) aligned_alloc(READ_BLOCK_SIZE, READ_BLOCK_SIZE);
    if (buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate aligned buffer\n");
        exit(EXIT_FAILURE);
    }

    //=====================
    // Actual file copying
    //=====================

    for (uint32_t i = 0U; i < src_size;)
    {
        ssize_t bytes_read = read(src_fd, buffer, READ_BLOCK_SIZE);
        if (bytes_read == -1)
        {
            fprintf(stderr, "Unable to read block [%x, %x)\n", i, i + READ_BLOCK_SIZE);
            exit(EXIT_FAILURE);
        }

        ssize_t bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written == -1 || bytes_written != bytes_read)
        {
            fprintf(stderr, "Unable to write block [%x, %lx)\n", i, i + bytes_read);
            exit(EXIT_FAILURE);
        }

        i += bytes_read;
        if (bytes_read != READ_BLOCK_SIZE)
        {
            break;
        }
    }

    //============================
    // End of actual file copying
    //============================

    close_src_dst_files(argv[1], src_fd, src_size, argv[2], dst_fd);

    return EXIT_SUCCESS;
}
