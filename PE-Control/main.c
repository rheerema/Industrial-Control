//--------------------------------------------------------------------
// Printer Emulator Control Program
//     This program is called by the user interface to make a request into
//     the 1022 using the Printer Emulator protocol.  The request is passed
//     as the first argument (arg1).  The return value indicates success
//     or failure of the request.
//--------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>   // atoi
#include <string.h>   // strcpy
#include <unistd.h>   // usleep

#include "../Common/message_services.h"

//--------------------------------------------------------------------
// File scope variables
//--------------------------------------------------------------------
// Version String
const char version_stg[] = {"v1.3.1"};

client_req c_msg;
server_rsp s_msg;
int isLogMode = 0;

//--------------------------------------------------------------------
// action()
//     Receive a command code character, validate, and dispatch messages
//  returns:
//       0  success
//      -1  failure
//--------------------------------------------------------------------
int action( char char_in )
{
	int rv = 0;

	printf("Command code in: %c\n", char_in );

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

#if 0
	// No support for quit, exit, or ESC character
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
#endif

	default:
		printf("Unknown character %c received: IGNORED\r\n", char_in);
		rv = -1;
		break;
	}

	return rv;
}

//--------------------------------------------------------------------
// main()
//--------------------------------------------------------------------
int main( int argc, char *argv[] )
{
	int rv, exit_value;
	int response_complete = 0;

	printf("\nPrinter Emulator Control %s\n", version_stg);
	printf("(c) 2025 Liquid Solids Control\n\n");

	if( argc !=2 ) {
		// No command argument was specified
		printf("Error - no command code provided\n");
		return EXIT_FAILURE;
	}

	// argv[1] is a null terminated char string so verify there is only
	// one character in it.  This is the command code.
	if( strlen(argv[1]) != 1 ) {
		printf("Error - too many command code characters\n");
		return EXIT_FAILURE;
	}

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

#if 1
	// NOTE: there is no need to send a CLIENT_INIT message.  Each
	// client request has the current client mqid so the server always
	// knows where to respond to
#else
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
#endif

	// Take action on the specified command code
	rv = action( argv[1][0] );
	if( rv == -1 ) {
		printf("Error - invalid command code or sending error\n");
		return EXIT_FAILURE;
	}	

	// Having sent our request we now wait for multiple responses
	// We presume success unless there is a subsequent failure
	exit_value = EXIT_SUCCESS;
	
	while( !response_complete )
	{
		// Loop until all responses are received or we encounter failure
		memset( &s_msg, 0, sizeof(s_msg) );

		// Receive any server responses.  Specify blocking
		rv = msg_rcv_from_server( &s_msg, RCV_BLOCKING );
		if( rv == 0 ) {
			// No messages on queue, do nothing
		} else if( rv == -1 ) {
			printf("Server response FAILED\r\n");
			exit_value = EXIT_FAILURE;
			response_complete = 1;
		} else {
			switch(s_msg.mtype)
			{
			case SERVER_REQUEST_SUCCESS:
				printf("SERVER_REQUEST_SUCCESS\r\n");
#if 0
				if( !strcmp(s_msg.rsp, "exit") ) {
				response_complete = 1;
				isExit = -1;
				}
#endif
				// This is an intermediary response so remain in loop
				break;
			case SERVER_REQUEST_FAILURE:
				printf("SERVER_REQUEST_FAILURE\r\n");
				printf("Server response: %s\r\n", s_msg.rsp);
				exit_value = EXIT_FAILURE;
				// Server Request Failure is not yet coded on Printer-Emulator
				response_complete = 1;
				break;
			case SERVER_ACTION_SUCCESS:
				printf("SERVER_ACTION_SUCCESS\r\n");
				printf("Server response: %s\r\n", s_msg.rsp);
				if( !strcmp(s_msg.rsp, "logmode") ) {
					isLogMode = atoi( &s_msg.rsp[8]);
					printf("%s\r\n", s_msg.rsp);
					printf( "client logmode %d\r\n", isLogMode );
				}
				response_complete = 1;
				break;
			case SERVER_ACTION_FAILURE:
				printf("SERVER_ACTION_FAILURE\r\n");
				printf("Server response: %s\r\n", s_msg.rsp);
				exit_value = EXIT_FAILURE;
				// Server Action Failure is not yet coded on Printer-Emulator
				response_complete = 1;
				break;
			case SERVER_RESET:
				printf("SERVER_RESET\r\n");
				printf("Server response: %s\r\n", s_msg.rsp);
				// This is an intermediary response so remain in loop
				break;
			}

#if 0
			// Experiment, sleep 2 sec.
			usleep(2000*1000);
#endif
		}
	}

	// Remove the client message queue
	rv = msg_remove_client_mq();
	if( rv == -1 ) {
		perror("msgctl");
		exit_value = EXIT_FAILURE;
	} else {
		printf("Client message queue removed\r\n");
	}
	
	return exit_value;
}
