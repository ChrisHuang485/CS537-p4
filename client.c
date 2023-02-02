#include <stdio.h>
#include <string.h>

#include "udp.h"
#include "mfs.h"
#include "msg.h"

#define BUFFER_SIZE 4096
char buffer[BUFFER_SIZE];

extern struct sockaddr_in socket_addr;
extern int sd;

int main(int argc, char *argv[])
{

    int port_num = atoi(argv[1]);
    
    int rc = MFS_Init(argv[2], port_num);
    assert(rc == 0);

    printf("client:: Init complete (rc=%d)\n", rc);

    // send init msg
    MSG_t msg;
    msg.msg_type = INIT_t;

    rc = UDP_Write(sd, &socket_addr, (char *)&msg, sizeof(MSG_t));
    printf("client:: UDP_Write rc=%d\n", rc);
    if (rc > 0)
    {
        struct sockaddr_in raddr;
        int rc = UDP_Read(sd, &raddr, buffer, BUFFER_SIZE);
        printf("client:: read %d bytes (message: '%s')\n", rc, buffer);
    }

    // send creat msg
    // MSG_t msg2;
    // msg2.msg_type = CRpwEAT_t;
    // msg2.inum = 0;
    // strpy(msg2.name, "name");
    char name[] = "name";

    rc = MFS_Creat(0, MFS_REGULAR_FILE, name);
    printf("client:: create msg sent (rc=%d)\n", rc);

    MFS_Shutdown();
    return 0;
}
