#include <sys/select.h>
#include <sys/time.h>

#include "mfs.h"
#include "udp.c"
#include "msg.h"

#define BUFFER_SIZE 4096

// global vars
struct sockaddr_in socket_addr;
MSG_t request_msg;
MSG_t response_msg;
int sd = -1;

MSG_t send_request(MSG_t request)
{
    printf("libmfs::  msg sending (type: %d, inum: %d, nbytes: %d; offset: %d; name: %s)\n",
           request.msg_type, request.inum, request.nbytes, request.offset, (char *)request.name);

    struct sockaddr_in read_addr;
    MSG_t response;

    // UDP_Write(sd, &socket_addr, (char *)&request, sizeof(MSG_t));
    // UDP_Read(sd, &read_addr, (char *)&response, sizeof(MSG_t));

    fd_set read_fdset;
    int max_retry_time = 5;
    int retry_cnt = 0;
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    do
    {
        FD_ZERO(&read_fdset);
        FD_SET(sd, &read_fdset);
        UDP_Write(sd, &socket_addr, (char *)&request, sizeof(MSG_t));
        int returned_val = select(sd + 1, &read_fdset, NULL, NULL, &timeout);

        if (returned_val > 0)
        {
            if (UDP_Read(sd, &read_addr, (char *)&response, sizeof(MSG_t)) > 0)
                return response;
            else
                retry_cnt++;
        }
    } while (retry_cnt < max_retry_time);

    return response;
}

int MFS_Init(char *hostname, int port)
{
    printf("libmfs::  initializing.\n");
    sd = UDP_Open(0);
    if (sd < 0) {
        printf("debug::  init failed, sd=%d\n", sd);
        return -1;
    }


    int rc = UDP_FillSockAddr(&socket_addr, hostname, port);
    if (rc < 0) {
        printf("debug::  init failed, rc=%d\n", rc);
        return -1;
    }
        
    // request_msg.msg_type = INIT_t;
    return 0;
}

int MFS_Lookup(int pinum, char *name)
{
    if (sd < 0 || strlen(name) > 28)
        return -1;

    request_msg.inum = pinum;
    strcpy((char *)request_msg.name, name);
    request_msg.msg_type = LOOKUP_t;
    return send_request(request_msg).rc;
}

int MFS_Stat(int inum, MFS_Stat_t *m)
{
    if (sd < 0)
        return -1;

    request_msg.inum = inum;
    request_msg.msg_type = STAT_t;

    response_msg = send_request(request_msg);
    m->size = response_msg.nbytes;
    m->type = response_msg.type;
    return response_msg.rc;
}

int MFS_Write(int inum, char *buffer, int offset, int nbytes)
{
    if (sd < 0 || offset / BUFFER_SIZE >= 30 || (nbytes < 0 || nbytes > BUFFER_SIZE))
        return -1;

    request_msg.inum = inum;
    memcpy((char *)request_msg.buffer, buffer, BUFFER_SIZE);
    request_msg.nbytes = nbytes;
    request_msg.offset = offset;
    request_msg.msg_type = WRITE_t;

    return send_request(request_msg).rc;
}

int MFS_Read(int inum, char *buffer, int offset, int nbytes)
{
    if (sd < 0)
        return -1;

    request_msg.inum = inum;
    request_msg.nbytes = nbytes;
    request_msg.offset = offset;
    request_msg.msg_type = READ_t;

    response_msg = send_request(request_msg);
    memcpy(buffer, response_msg.buffer, MFS_BLOCK_SIZE);
    return response_msg.rc;
}

int MFS_Creat(int pinum, int type, char *name)
{
    if (sd < 0)
        return -1;

    request_msg.inum = pinum;
    request_msg.type = type;
    strcpy((char *)request_msg.name, name);
    request_msg.msg_type = CREAT_t;
    return send_request(request_msg).rc;
}

int MFS_Unlink(int pinum, char *name)
{
    if (sd < 0)
        return -1;

    request_msg.inum = pinum;
    strcpy((char *)request_msg.name, name);
    request_msg.msg_type = UNLINK_t;

    return send_request(request_msg).rc;
}

int MFS_Shutdown()
{
    if(sd < 0) 
        return -1;
    
    request_msg.msg_type = SHUTDOWN_t;
    return send_request(request_msg).rc;
}