#ifndef __MSG_h__
#define __MSG_h__

#define INIT_t 1
#define LOOKUP_t 2
#define STAT_t 3
#define WRITE_t 4
#define READ_t 5
#define CREAT_t 6
#define UNLINK_t 7
#define SHUTDOWN_t 8

typedef struct __MSG_t{
    int msg_type; // message type
    int rc; 

    int inum; 
    int nbytes;
    int type;
    int offset;

    char name[28]; // file or dir name
    char buffer[4096]; // data

} MSG_t;

#endif