//--------------------------------------------------------------------
// Printer Emulator Console
//     Console command line control of the Printer Emulator (printem).
//     This program allows a user to request a Report, History or Log from
//     the 1022.  It communicates with printem over a message queue.
// 
//--------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>   // atoi
#include <string.h>   // strcpy
#include <signal.h>
#include <errno.h>    // Error integer and strerror() function

#include "../Common/message_services.h"
#include "./client_utils.h"

//--------------------------------------------------------------------
// File scope variables
//--------------------------------------------------------------------
// Version String
const char version_stg[] = {"v1.3.1"};
int isBreak = 0;
client_req c_msg;
server_rsp s_msg;
int isLogMode = 0;

//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
// Future Enhancement
//   add a state machine that is called by action().  In action() it is determined, based
//   on state if the request should be sent or denied.  Reason for denial is the server side
//   has not yet acknowledged success.  For example if you request a Report and then
//   immediately request another Report before the server has finished the first request
//   the state machine code will reject the second request.  It won't even send it to the
//   server.
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

//--------------------------------------------------------------------
// action()
//     Receive stdin characters (as an int), validate, and dispatch messages
//--------------------------------------------------------------------
void action( int char_in )
{
	int rv;
	
	switch( char_in )
	{
	case 'h':
	case 'H':
		// Request History listing
		memset( &c_msg, 0, sizeof(c_msg) );
	    c_msg.mtype = CLIENT_REQ_HISTORY;
		c_msg.client_id = msg_get_client_mq();
	    strcpy(c_msg.cmd, "history");

		rv = msg_send_to_server( &c_msg );
		if( rv == -1 ) {
			printf("History request FAILED\r\n");
		} else {
			printf("History requested\r\n");
		}
		break;
	case 'l':
	case 'L':
		// Toggle Log mode
		memset( &c_msg, 0, sizeof(c_msg) );
	    c_msg.mtype = CLIENT_REQ_LOG;
		c_msg.client_id = msg_get_client_mq();
	    strcpy(c_msg.cmd, "log");

		rv = msg_send_to_server( &c_msg );
		if( rv == -1 ) {
			printf("Log toggle request FAILED\r\n");
		} else {
			printf("Log toggle requested\r\n");
		}
		break;
	case 'r':
	case 'R':
		// Request a Report, prepare the message
		memset( &c_msg, 0, sizeof(c_msg) );
	    c_msg.mtype = CLIENT_REQ_REPORT;
		c_msg.client_id = msg_get_client_mq();
	    strcpy(c_msg.cmd, "report");

		rv = msg_send_to_server( &c_msg );
		if( rv == -1 ) {
			printf("Report request FAILED\r\n");
		} else {
			printf("Report requested\r\n");
		}
		break;
	case 'q':
	case 'Q':
		// Quit client
		printf("\r\n");
		isBreak = 1;
		break;
	case 'x':
	case 'X':
		// eXit Printer Emulator: kill the server and quit the client
		printf("\r\n");
		// Send exit message
		memset( &c_msg, 0, sizeof(c_msg) );
	    c_msg.mtype = CLIENT_REQ_EXIT;
		c_msg.client_id = msg_get_client_mq();
	    strcpy(c_msg.cmd, "exit");

		rv = msg_send_to_server( &c_msg );
		if( rv == -1 ) {
			printf("Exit request FAILED\r\n");
		} else {
			printf("Exit requested\r\n");
		}
		break;
	case 27:
		// ESC hit, quit the client
		printf("\r\n");
		isBreak = 1;
		break;
	default:
		printf("Unknown character %c hit: IGNORED\r\n", char_in);
		break;
	}
}

//--------------------------------------------------------------------
// response()
//     Receive a response from the Printer Emulator server
//  returns:
//       0  no action
//      -1  exit
//--------------------------------------------------------------------
int response( void )
{
	int rv;
	int isExit = 0;
	
	memset( &s_msg, 0, sizeof(s_msg) );

	// Receive any server responses.  Specify non-blocking
	rv = msg_rcv_from_server( &s_msg, RCV_NON_BLOCKING );
	if( rv == 0 ) {
		// No messages on queue, do nothing
	} else if( rv == -1 ) {
		printf("Server response FAILED\r\n");
	} else {
		switch(s_msg.mtype)
		{
		case SERVER_REQUEST_SUCCESS:
			printf("SERVER_REQUEST_SUCCESS\r\n");
			if( !strcmp(s_msg.rsp, "exit") ) {
				isBreak = 1;
				isExit = -1;
			}
			break;
		case SERVER_REQUEST_FAILURE:
			printf("SERVER_REQUEST_FAILURE\r\n");
			printf("Server response: %s\r\n", s_msg.rsp);
			break;
		case SERVER_ACTION_SUCCESS:
			printf("SERVER_ACTION_SUCCESS\r\n");
			printf("Server response: %s\r\n", s_msg.rsp);
			if( !strcmp(s_msg.rsp, "logmode") ) {
				isLogMode = atoi( &s_msg.rsp[8]);
				printf("%s\r\n", s_msg.rsp);
				printf( "client logmode %d\r\n", isLogMode );
			}
			break;
		case SERVER_ACTION_FAILURE:
			printf("SERVER_ACTION_FAILURE\r\n");
			printf("Server response: %s\r\n", s_msg.rsp);
			break;
		case SERVER_RESET:
			printf("SERVER_RESET\r\n");
			printf("Server response: %s\r\n", s_msg.rsp);
			break;
		}
	}
	return isExit;
}

//--------------------------------------------------------------------
// SIGINT handler
//--------------------------------------------------------------------
void INThandler( int sig )
{
	isBreak = 1;
}

//--------------------------------------------------------------------
// main()
//--------------------------------------------------------------------
int main( int argc, char *argv[] )
{
	int rv;
	int char_in;

	printf("\nPrinter Emulator Console %s\n", version_stg);
	printf("(c) 2025 Liquid Solids Control\n\n");

	signal(SIGINT, INThandler);
	set_conio_terminal_mode();
	
    // Open the Server message queue from well-known key
	rv = msg_get_server_mq();
    if (rv == -1) {	
		//perror("server msgget");
		printf("Unable to open printem message queue\r\n");
		printf("Exiting\r\n");
		exit(EXIT_FAILURE);
    }
	
	// Create the Client message queue
	rv = msg_create_client_mq();
    if (rv == -1) {
        //perror("client msgget");
		printf("Unable to create peconsole message queue\r\n");
		printf("Exiting\r\n");
        exit(EXIT_FAILURE);
    }

	// Notify the Server of our message queue
	memset( &c_msg, 0, sizeof(c_msg) );
	c_msg.mtype = CLIENT_INIT;
	c_msg.client_id = msg_get_client_mq();
	strcpy(c_msg.cmd, "init");

	rv = msg_send_to_server( &c_msg );
	if( rv == -1 ) {
		printf("CLIENT_INIT send FAILED!\r\n");
        exit(EXIT_FAILURE);
	} else {
		printf("Client Init requested\r\n");
	}
	
	// - - - - - - - - - M a i n   L o o p - - - - - - - - -
	while( !isBreak )
	{
		// Non-blocking check if a key is hit.  If not we stay in
		// this loop waiting
		while( !kbhit() )
		{
			// Non-blocking code goes here
			rv = response();
			if( rv == -1 )  break;
			
			//printf("* ");  fflush(stdout);
			
			// Sleep 10 mS to let other threads run
			// 1 mS is 1000 uS
			usleep(10*1000);
		}

		// Check if exiting
		if( rv == -1 )  break;
		
		// Act on the typed character
		char_in = getch();
		action( char_in );
	}
	// - - - - - - - E N D  M a i n   L o o p - - - - - - - - -
	
    // Remove the client message queue
	rv = msg_remove_client_mq();
	if( rv == -1 ) {
		perror("msgctl");
	} else {
		printf("Client message queue removed\r\n");
	}
	
    return EXIT_SUCCESS;
}
