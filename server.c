#include <stdio.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <signal.h>

#include "udp.h"
#include "mfs.h"
#include "ufs.h"
#include "msg.h"

#define BUFFER_SIZE 4096

typedef struct
{
    dir_ent_t entries[BUFFER_SIZE / sizeof(dir_ent_t)];
} dir_pack_t;

// global vars
inode_t empty_inode;
int server_img_fd;
int sd;
int *server_file;
super_t *superblock_addr;
inode_t *inode_area;
dir_pack_t *data_area;

void interruption_handler()
{
    UDP_Close(sd);
    exit(130); // exit by SIGINT
}

void print_usage()
{
    fprintf(stderr, "usage: server [portnum] [file-system-image]\n");
    exit(1);
}

void *block_addr_to_addr(int block_addr)
{
    return (char *)superblock_addr + block_addr * BUFFER_SIZE;
}

int get_ith_bit(unsigned int *bitmap, int ith)
{
    int block_idx = ith / 32;
    int offset = 31 - (ith % 32);
    return (bitmap[block_idx] >> offset) & 0x1;
}

void set_ith_bit(unsigned int *bitmap, int ith, int val)
{
    // val should be either 0 or 1.
    int block_idx = ith / 32;
    int offset = 31 - (ith % 32);
    if (val)
        bitmap[block_idx] |= 0x1 << offset;
    else
        bitmap[block_idx] &= ~(0x1 << offset);
}

int get_available_inum()
{
    for (int i = 0; i < superblock_addr->num_inodes; i++)
    {
        int t = get_ith_bit(block_addr_to_addr(superblock_addr->inode_bitmap_addr), i);
        if (t == 0)
            return i;
    }
    return -1; // not found
}

int get_available_datablock()
{
    for (int i = 0; i < superblock_addr->num_data; i++)
    {
        int t = get_ith_bit(block_addr_to_addr(superblock_addr->data_bitmap_addr), i);
        if (t == 0)
            return i;
    }
    return -1; // not found
}

int server_lookup(int pinum, char *name)
{
    if (pinum < 0 || pinum >= superblock_addr->num_inodes)
        return -1;

    if (inode_area[pinum].type != MFS_DIRECTORY)
        return -1;

    for (int i = 0; i < DIRECT_PTRS; i++)
    {
        if (inode_area[pinum].direct[i] == -1)
            continue;

        int block_idx = inode_area[pinum].direct[i] - superblock_addr->data_region_addr;

        for (int j = 0; j < 128; j++)
        {
            dir_ent_t entry = data_area[block_idx].entries[j];

            if (strcmp(entry.name, name) == 0)
                return entry.inum;
        }
    }
    return -1;
}

inode_t server_stat(int inum)
{
    if (inum < 0 || inum >= superblock_addr->num_inodes)
        return empty_inode;

    return inode_area[inum];
}

int server_write(int inum, char *buffer, int offset, int nbytes)
{
    if (nbytes < 0 || nbytes > BUFFER_SIZE || offset / BUFFER_SIZE >= 30)
        return -1;

    if (inum < 0 || inum >= superblock_addr->num_inodes)
        return -1;

    if (inode_area[inum].type == MFS_DIRECTORY)
        return -1;

    int block_idx = inode_area[inum].direct[offset / BUFFER_SIZE] - superblock_addr->data_region_addr;
    memcpy(&data_area[block_idx].entries, buffer, nbytes);

    inode_area[inum].size += nbytes;
    return 0;
}

int server_read(int inum, char *buffer, int offset, int nbytes)
{
    if (nbytes < 0 || nbytes > BUFFER_SIZE || offset / BUFFER_SIZE >= 30)
        return -1;

    int block_idx = inode_area[inum].direct[offset / BUFFER_SIZE] - superblock_addr->data_region_addr;
    if (inode_area[inum].type == MFS_REGULAR_FILE)
        memcpy(buffer, &data_area[block_idx].entries, nbytes);
    else
        memcpy((MFS_DirEnt_t *)buffer, &data_area[block_idx].entries, nbytes);
    return 0;
}

int server_create(int pinum, int type, char *name)
{
    if (strlen(name) > 28)
    {
        printf("server:: invalid name, creating failed.");
        return -1;
    }

    if (pinum < 0 || pinum >= superblock_addr->num_inodes)
        return -1;

    if (inode_area[pinum].type != MFS_DIRECTORY)
    {
        printf("server:: parent type != MFS_DIRECTORY, creating failed.");
        return -1;
    }

    int block_idx = inode_area[pinum].direct[0] - superblock_addr->data_region_addr;
    int next_inum = get_available_inum();
    printf("server:: next inum: %d\n", next_inum);

    for (int i = 0; i < BUFFER_SIZE / sizeof(dir_ent_t); i++)
    {
        if (data_area[block_idx].entries[i].inum == -1)
        {
            printf("server:: block %d's inum is written to %d\n", i, next_inum);

            // data block setup
            data_area[block_idx].entries[i].inum = next_inum;
            strcpy(data_area[block_idx].entries[i].name, name);

            set_ith_bit(block_addr_to_addr(superblock_addr->inode_bitmap_addr), next_inum, 1);
            inode_area[pinum].size += sizeof(dir_ent_t);
            break;
        }
    }

    inode_area[next_inum].type = type;

    if (type == MFS_DIRECTORY) // new directory
    {
        inode_area[next_inum].size = 2 * sizeof(dir_ent_t);

        dir_ent_t entries[128];
        strcpy(entries[0].name, "."); // current dir
        entries[0].inum = next_inum;
        strcpy(entries[1].name, ".."); // parent dir
        entries[1].inum = pinum;

        for (int i = 1; i < DIRECT_PTRS; i++)
        {
            inode_area[next_inum].direct[i] = -1; // unused
        }
        for (int i = 2; i < BUFFER_SIZE / sizeof(dir_ent_t); i++)
        {
            entries[i].inum = -1; // unused
        }

        // get 1 datablock
        int next_datablock = get_available_datablock();
        inode_area[next_inum].direct[0] = next_datablock + superblock_addr->data_region_addr;
        set_ith_bit(block_addr_to_addr(superblock_addr->data_bitmap_addr), next_datablock, 1);

        memcpy(&data_area[next_datablock].entries, entries, BUFFER_SIZE);
    }
    else // new file
    {
        for (int i = 0; i < DIRECT_PTRS; i++)
        {
            int next_datablock = get_available_datablock();
            inode_area[next_inum].direct[i] = next_datablock + superblock_addr->data_region_addr;
            set_ith_bit(block_addr_to_addr(superblock_addr->data_bitmap_addr), next_datablock, 1);
        }
    }
    return 0;
}

int server_unlink(int pinum, char *name)
{
    if (pinum < 0 || pinum >= superblock_addr->num_inodes)
        return -1;

    if (inode_area[pinum].type == MFS_REGULAR_FILE)
        return -1;

    int block_idx = inode_area[pinum].direct[0] - superblock_addr->data_region_addr;
    for (int i = 0; i < BUFFER_SIZE / sizeof(dir_ent_t); i++)
    {
        if (strcmp(data_area[block_idx].entries[i].name, name) == 0)
        {
            int target_inum = data_area[block_idx].entries[i].inum;

            if (inode_area[target_inum].type == MFS_DIRECTORY) // unlink a directory
            {
                // dir is not empty
                if (inode_area[target_inum].size > 2 * sizeof(dir_ent_t))
                    return -1;

                int unlink_data_idx = inode_area[target_inum].direct[0] - superblock_addr->data_region_addr;
                for (int i = 0; i < 2; i++) // delete `.` and `..`
                {
                    set_ith_bit(block_addr_to_addr(superblock_addr->inode_bitmap_addr),
                                data_area[unlink_data_idx].entries[i].inum, 0);

                    data_area[unlink_data_idx].entries[i].inum = -1;
                    strcpy(data_area[unlink_data_idx].entries[i].name, "\0");
                }
                set_ith_bit(block_addr_to_addr(superblock_addr->data_bitmap_addr),
                            inode_area[target_inum].direct[0] - superblock_addr->data_region_addr, 0);

                inode_area[target_inum].direct[0] = -1;
            }
            else // unlink a file
            {
                for (int i = 0; i < DIRECT_PTRS; i++)
                {
                    int unlink_data_idx = inode_area[target_inum].direct[i] - superblock_addr->data_region_addr;

                    for (int j = 0; j < BUFFER_SIZE / sizeof(dir_ent_t); j++)
                    {
                        set_ith_bit(block_addr_to_addr(superblock_addr->inode_bitmap_addr),
                                    data_area[unlink_data_idx].entries[i].inum, 0);
                        data_area[unlink_data_idx].entries[i].inum = -1;
                        strcpy(data_area[unlink_data_idx].entries[i].name, "\0");
                    }
                    set_ith_bit(block_addr_to_addr(superblock_addr->data_bitmap_addr),
                                inode_area[target_inum].direct[i] - superblock_addr->data_region_addr, 0);

                    inode_area[target_inum].direct[i] = -1;
                }
            }
            inode_area[target_inum].size = 0;
            inode_area[target_inum].type = 0;

            data_area[block_idx].entries[i].inum = -1;
            strcpy(data_area[block_idx].entries[i].name, "\0");

            inode_area[pinum].size -= sizeof(dir_ent_t);
            break;
        }
    }

    return 0;
}

void save_server_file()
{
    lseek(server_img_fd, superblock_addr->inode_bitmap_addr * BUFFER_SIZE, SEEK_SET);
    write(server_img_fd, block_addr_to_addr(superblock_addr->inode_bitmap_addr), BUFFER_SIZE);
    printf("server:: inode bitmap saved\n");

    lseek(server_img_fd, superblock_addr->data_bitmap_addr * BUFFER_SIZE, SEEK_SET);
    write(server_img_fd, block_addr_to_addr(superblock_addr->data_bitmap_addr), BUFFER_SIZE);
    printf("server:: data bitmap saved\n");

    for (int i = 0; i < superblock_addr->num_inodes; i++)
    {
        lseek(server_img_fd, i * sizeof(inode_t) + superblock_addr->inode_region_addr * BUFFER_SIZE, SEEK_SET);
        write(server_img_fd, &inode_area[i], sizeof(inode_t));
    }
    printf("server:: inode blocks saved\n");

    for (int i = 0; i < superblock_addr->num_data; i++)
    {
        lseek(server_img_fd, i * BUFFER_SIZE + superblock_addr->data_region_addr * BUFFER_SIZE, SEEK_SET);
        write(server_img_fd, &(data_area[i].entries), BUFFER_SIZE);
    }
    printf("server:: data blocks saved\n");
    

    fsync(server_img_fd);
    printf("server:: fsync completed\n");
}

// server code
int main(int argc, char *argv[])
{
    // check num of args
    if (argc != 3)
    {
        print_usage();
    }

    // get args
    int port = atoi(argv[1]);
    char *fs_img = argv[2];

    // initialization
    empty_inode.type = -1;

    server_img_fd = open(fs_img, O_RDWR, S_IRWXU | S_IRUSR);
    assert(server_img_fd >= 0);

    struct stat server_img_stat;
    assert(fstat(server_img_fd, &server_img_stat) >= 0); // get stat info of the server file image

    sd = UDP_Open(port);
    assert(sd > -1);
    signal(SIGINT, interruption_handler);

    server_file = mmap(NULL, server_img_stat.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, server_img_fd, 0);
    superblock_addr = (super_t *)server_file;

    data_area = malloc(BUFFER_SIZE * superblock_addr->num_data);
    inode_area = malloc(BUFFER_SIZE * superblock_addr->inode_region_len);

    // load data to `data_area`
    for (int i = 0; i < superblock_addr->num_data; i++)
    {
        lseek(server_img_fd, i * BUFFER_SIZE + superblock_addr->data_region_addr * BUFFER_SIZE, SEEK_SET);
        read(server_img_fd, &(data_area[i].entries), BUFFER_SIZE);
    }

    // load inodes to `inode_area`
    for (int i = 0; i < superblock_addr->num_inodes; i++)
    {
        lseek(server_img_fd, i * sizeof(inode_t) + superblock_addr->inode_region_addr * BUFFER_SIZE, SEEK_SET);
        read(server_img_fd, &inode_area[i], sizeof(inode_t));
    }

    while (1)
    {
        struct sockaddr_in socket_addr;
        printf("server:: waiting...\n");

        MSG_t request_msg;
        int rc = UDP_Read(sd, &socket_addr, (char *)&request_msg, sizeof(MSG_t));
        printf("server:: read message [size:%d, mtype:%d, name:%s, inode:%d]\n", rc, request_msg.msg_type, (char *)request_msg.name, request_msg.inum);

        if (rc > 0)
        {
            MSG_t response_msg;
            switch (request_msg.msg_type)
            {
            case INIT_t:
                printf("server:: init\n");
                response_msg.rc = 0;
                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case LOOKUP_t:
                printf("server:: lookup\n");
                response_msg.rc = server_lookup(request_msg.inum, request_msg.name);
                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case STAT_t:
                printf("server:: stat\n");
                response_msg.rc = -1;
                inode_t inode = server_stat(request_msg.inum);

                response_msg.rc = 0;
                response_msg.nbytes = inode.size;
                response_msg.type = inode.type;
                printf("size: %d, type: %d\n", response_msg.nbytes, response_msg.type);

                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case WRITE_t:
                printf("server:: write\n");
                response_msg.rc = server_write(request_msg.inum, request_msg.buffer, request_msg.offset, request_msg.nbytes);

                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case READ_t:
                printf("server:: read\n");
                char *buffer = (char *)malloc(BUFFER_SIZE);
                response_msg.rc = server_read(request_msg.inum, buffer, request_msg.offset, request_msg.nbytes);
                memcpy(response_msg.buffer, buffer, BUFFER_SIZE);

                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                free(buffer);
                break;

            case CREAT_t:
                printf("server:: create\n");
                response_msg.rc = server_create(request_msg.inum, request_msg.type, request_msg.name);
                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case UNLINK_t:
                printf("server:: unlink\n");
                response_msg.rc = server_unlink(request_msg.inum, request_msg.name);
                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));
                break;

            case SHUTDOWN_t:
                printf("server:: shutdown\n");

                response_msg.rc = 0;
                UDP_Write(sd, &socket_addr, (char *)&response_msg, sizeof(MSG_t));

                save_server_file();

                // free(data_area);
                // free(inode_area);
                // free(server_file);
                UDP_Close(sd);
                close(server_img_fd);
                printf("server:: exiting...\n");
                exit(0);
                break;

            default:
                break;
            }
        }
    }

    save_server_file();

    free(data_area);
    free(inode_area);
    free(server_file);
    UDP_Close(sd);
    close(server_img_fd);

    return 0;
}
