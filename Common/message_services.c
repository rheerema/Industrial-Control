
//--------------------------------------------------------------------
// Message Services
//--------------------------------------------------------------------

#include "message_services.h"

// Private to Message Services

// Server side data
int server_mq;         // server obtained server_mq
int client_mq_server;  // client_mq received on server side

// Client side data
int client_mq;         // client_mq created by client side
int server_mq_client;  // client obtained server_mq


// = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
//                         S E R V E R  S i d e
// = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =


//--------------------------------------------------------------------
//  msg_create_server_mq()
//      Sets server_mq
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_create_server_mq( void )
{
	// Create (or open existing) server message queue.  Allow read and write
	// for User, Group, and Other
    server_mq = msgget(MSG_QUEUE_KEY, IPC_CREAT | S_IRUSR | S_IWUSR
								| S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    return (server_mq == -1) ? -1 : 0;
}	

//--------------------------------------------------------------------
//  msg_rcv_from_client()
//      non-blocking receive on server_mq
//      the entire cmd field is copied by msgrcv().
//      Must be a null terminated string
//  returns:
//      num bytes in cmd field (if message present)
//      0 if no message present
//      -1 failure (with errno set)
//--------------------------------------------------------------------
ssize_t msg_rcv_from_client( client_req* c_msg )
{
	ssize_t count = msgrcv(server_mq, c_msg, sizeof(c_msg->cmd), 0, IPC_NOWAIT);
	if( count == -1 )
	{
		switch( errno )
		{
		case ENOMSG:
			// In non-blocking mode and there are no messages on queue
			// (the normal case)
			count = 0;
			break;
		case EIDRM:
			// message queue removed.  Have not seen this yet.
			printf("msgrcv: Message queue removed EIDRM\n");
			break;
		case EINVAL:
			// msgid is invalid (queue removed?)
			printf("msgrcv: Message queue removed EINVAL\n");
			break;
		case EACCES:
			// calling process, no read permission on message queue
			printf("msgrcv: No read permission\n");				
			break;
		default:
			perror("msgrcv");
			break;
		}
	} else {
		if( c_msg->mtype == CLIENT_INIT )
		{
			// Capture the server mq id
			client_mq_server = c_msg->client_id;
		}
		else
		{
			// If printem server restarts then we need to update the
			// server copy of the client mqid.  printem will not be receiving
			// a CLIENT_INIT
			if( client_mq_server != c_msg->client_id ) {
				client_mq_server = c_msg->client_id;
			}
		}
	}
	
	return count;
}

//--------------------------------------------------------------------
//  msg_send_to_client()
//      Send a message to the client
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_send_to_client( server_rsp *s_msg )
{
	int rv = msgsnd(client_mq_server, s_msg, sizeof(s_msg->rsp), 0);
	if( rv == -1 )
	{
		switch( errno )
		{
		case EACCES:
			// calling process, no write permission on message queue
			printf("msgsnd: No write permission on message queue\n");
			break;
		case EINVAL:
			printf("msgsnd: Invalid msgid value\n");
			break;
		case EAGAIN:
			// if msgflg parm was set for IPC_NOWAIT and queue was full
			// it would not block and throw this error
			printf("msgsnd: EAGAIN error\n");
			break;
		default:
	        perror("msgsnd");
			break;
		}
	}
	return rv;
}

//--------------------------------------------------------------------
//  msg_remove_server_mq()
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_remove_server_mq( void )
{
    int rv = msgctl(server_mq, IPC_RMID, NULL);
	return rv;
}

// = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =
//                     C L I E N T  S i d e
// = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = = =


//--------------------------------------------------------------------
//  msg_create_client_mq()
//      Called by client side, sets client_mq
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_create_client_mq( void )
{
    client_mq = msgget(IPC_PRIVATE,  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    return (client_mq == -1) ? -1 : 0;
}

//--------------------------------------------------------------------
//  msg_create_client_mq_ftok()
//      Called by client side, sets client_mq based on ftok() instead of IPC_PRIVATE
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_create_client_mq_ftok( const char *file )
{
	key_t key;
	int id;

	key = ftok( file, 'x' );
	if( key == -1 ) {
		return -1;
	}
    client_mq = msgget(key, IPC_CREAT | S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);

    return (client_mq == -1) ? -1 : 0;
}

//--------------------------------------------------------------------
//  msg_get_client_mq()
//      Client obtains the client_mq that is private to message
//      services.
//--------------------------------------------------------------------
int msg_get_client_mq( void )
{
	return client_mq;
}

//--------------------------------------------------------------------
//  msg_get_server_mq()
//      Called by client side, uses KEY to get server_mq
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_get_server_mq( void )
{
	server_mq_client = msgget( MSG_QUEUE_KEY, S_IWUSR );
	return (server_mq_client == -1) ? -1 : 0;
}


//--------------------------------------------------------------------
//  msg_send_to_server()
//      Send a message to the server
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_send_to_server( client_req *c_msg )
{
	// Send the message
	int rv = msgsnd(server_mq_client, c_msg, sizeof(c_msg->cmd), 0);
	if( rv == -1 )
	{
		switch( errno )
		{
		case EACCES:
			// calling process, no write permission on message queue
			printf("msgsnd: No write permission on message queue\n");
			break;
		case EINVAL:
			printf("msgsnd: Invalid msgid value\n");
			break;
		case EAGAIN:
			// if msgflg parm was set for IPC_NOWAIT and queue was full
			// it would not block and throw this error
			printf("msgsnd: EAGAIN error\n");
			break;
		default:
	        perror("msgsnd");
			break;
		}
	}
	return rv;
}

//--------------------------------------------------------------------
//  msg_rcv_from_server()
//      blocking or non-blocking receive on client_mq
//      The entire cmd field is copied by msgrcv().
//      Must be a null terminated string
//  returns:
//      num bytes in cmd field (if message present)
//      0 if no message present
//      -1 failure (with errno set)
//--------------------------------------------------------------------
ssize_t msg_rcv_from_server( server_rsp* s_msg, int is_blocking )
{
	int msg_flags;
	is_blocking ? msg_flags = 0 : msg_flags = IPC_NOWAIT;

	ssize_t count = msgrcv(client_mq, s_msg, sizeof(s_msg->rsp), 0, msg_flags);
	if( count == -1 )
	{
		switch( errno )
		{
		case ENOMSG:
			// In non-blocking mode and there are no messages on queue
			// (the normal case)
			count = 0;
			break;
		case EIDRM:
			// message queue removed.  Have not seen this yet.
			printf("msgrcv: Message queue removed EIDRM\n");
			break;
		case EINVAL:
			// msgid is invalid (queue removed?)
			printf("msgrcv: Message queue removed EINVAL\n");
			break;
		case EACCES:
			// calling process, no read permission on message queue
			printf("msgrcv: No read permission\n");				
			break;
		default:
			perror("msgrcv");
			break;
		}
	}
	
	return count;
}

//--------------------------------------------------------------------
//  msg_remove_clientt_mq()
//  returns:
//       0  success
//      -1  failure (with errno set)
//--------------------------------------------------------------------
int msg_remove_client_mq( void )
{
    int rv = msgctl(client_mq, IPC_RMID, NULL);
	return rv;
}

//--------------------------------------------------------------------
// Sample code for generating an ftok file and using it to create an MQ ID by calling
// the ftok version of msg_create_client_mq()
//--------------------------------------------------------------------
#if 0
	const char client_mq_file[] = { "./lsc_client_mq.txt" };

	// If it does not yet exist create a small file for use generating the
	// client message queue ID.  This is used by ftok()
	struct stat sb;  // stat buffer
	if( (stat(client_mq_file, &sb) == 0) && (S_ISREG(sb.st_mode)) )  {
		// The file exists
		printf("MQ ID file %s exists\n", client_mq_file);
	} else {
		// Creating the file
		char tmp_cmd[512];
		sprintf( tmp_cmd, "echo \"LSC client mqid\" > %s", client_mq_file );
		printf("%s\n", tmp_cmd);

		if( 0 == system( tmp_cmd ) ) {
			printf( "MQ ID file %s created\n", client_mq_file );
		} else {
			printf( "Error creating MQ ID file %s\n", client_mq_file );
			return EXIT_FAILURE;
		}
	}

	// Create the Client message queue
	rv = msg_create_client_mq_ftok( client_mq_file );
    if (rv == -1) {
        //perror("client msgget");
		printf("Error - Unable to create pecontrol message queue\r\n");
        exit(EXIT_FAILURE);
    }

    // When we're all done, remove the client message queue
	rv = msg_remove_client_mq();
	if( rv == -1 ) {
		perror("msgctl");
	} else {
		printf("Client message queue removed\r\n");
	}
#endif
