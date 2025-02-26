
//--------------------------------------------------------------------
// Message Services
//--------------------------------------------------------------------
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <errno.h>    // Error integer and strerror() function

#define MSG_QUEUE_KEY 0x1aaaaaa1

#define MSG_MAX_PAYLOAD 256

typedef struct client_req
{
	long mtype;
	int client_id;
	char cmd[MSG_MAX_PAYLOAD];
} client_req;

typedef struct server_rsp
{
	long mtype;
	char rsp[MSG_MAX_PAYLOAD];
} server_rsp;

// mtype types (all non-zero)
#define CLIENT_INIT         1
#define CLIENT_REQ_REPORT   2
#define CLIENT_REQ_HISTORY  3
#define CLIENT_REQ_LOG      4
#define CLIENT_REQ_EXIT     5

#define SERVER_REQUEST_SUCCESS  1
#define SERVER_REQUEST_FAILURE  2
#define SERVER_ACTION_SUCCESS   3
#define SERVER_ACTION_FAILURE   4
#define SERVER_RESET            5

// Server side message services
int msg_create_server_mq( void );
ssize_t msg_rcv_from_client( client_req* c_msg );
int msg_send_to_client( server_rsp *s_msg );
int msg_remove_server_mq( void );

// Client side message services
int msg_create_client_mq( void );
int msg_create_client_mq_ftok( const char *file );
int msg_get_client_mq( void );
int msg_get_server_mq( void );
int msg_send_to_server( client_req *c_msg );
ssize_t msg_rcv_from_server( server_rsp* s_msg, int is_blocking );
int msg_remove_client_mq( void );

// msg_rcv_from_server, specify blocking or non-blocking
#define RCV_BLOCKING     1
#define RCV_NON_BLOCKING 0
