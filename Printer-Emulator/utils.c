
//--------------------------------------------------------------------
//   utils.c
//--------------------------------------------------------------------
#include <stdlib.h>   // EXIT_SUCCESS, EXIT_FAILURE
#include <stdio.h>
#include <string.h>
#include <time.h>     // strftime()
#include <fcntl.h>    // Contains file controls like O_RDWR
#include <unistd.h>   // write(), read(), close(), usleep()
#include <errno.h>    // Error integer and strerror() function
#include <termios.h>  // Contains POSIX terminal control definitions

#include "utils.h"
#include "parser.h"
#include "../Common/message_services.h"

//--------------------------------------------------------------------
//  DumpHex()
//      Write data in hex dump format to a file
//--------------------------------------------------------------------
void DumpHex(const void* data, size_t size, FILE *f_out) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		fprintf(f_out, "%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			fprintf(f_out, " ");
			if ((i+1) % 16 == 0) {
				fprintf(f_out, "|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					fprintf(f_out, " ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					fprintf(f_out, "   ");
				}
				fprintf(f_out, "|  %s \n", ascii);
			}
		}
	}
}

// Termios structure included here for reference
#if 0
    struct termios {
    	tcflag_t c_iflag;		/* input mode flags */
    	tcflag_t c_oflag;		/* output mode flags */
    	tcflag_t c_cflag;		/* control mode flags */
    	tcflag_t c_lflag;		/* local mode flags */
    	cc_t c_line;			/* line discipline */
    	cc_t c_cc[NCCS];		/* control characters */
    };

#endif

//--------------------------------------------------------------------
// serial_port_open()
//
// Blocking Read
// VMIN = 1, VTIME = 0
//
// based on code found at:
// https://blog.mbedded.ninja/programming/operating-systems/linux/linux-serial-ports-using-c-cpp/
// Geoffrey Hunter  (6-24-2017)
//--------------------------------------------------------------------
int serial_port_open(int *serial_port, char *port_name)
{
	printf( "Open serial port %s\n", port_name );

	*serial_port = open( port_name, O_RDWR);

	// Check for errors
	if (*serial_port < 0) {
		printf("Error %i from open: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	// Create new termios struct, we call it 'tty' for convention
	// No need for "= {0}" at the end as we'll immediately write the existing
	// config to this struct
	struct termios tty;

	// Read in existing settings, and handle any error
	// NOTE: This is important! POSIX states that the struct passed to tcsetattr()
	// must have been initialized with a call to tcgetattr() overwise behaviour
	// is undefined
	if(tcgetattr(*serial_port, &tty) != 0) {
		printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	// Control modes  (c_cflags)
	// PARENB (Parity)  we're not using parity
	tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)

	// CSTOPB (Number of stop bits)  1 for us
	tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)

	// Number of Bits per Byte is set by CS<number>
	tty.c_cflag &= ~CSIZE; // First clear all the size bits, then set what you want
	tty.c_cflag |= CS8; // 8 bits per byte (most common)

	// Hardware Flow Control (CRTSCTS)  We are not using
	tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)

	// Modem signals, CREAD and CLOCAL.  Setting CLOCAL disables modem signals
	// which revents the controlling process from sending SIGHUP if carrier detect
	// is lost
	tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

	// Local Modes  (c_lflag)  Disable Canonical Mode!  Puts the port in RAW mode
	// I.e., don't look for a new line to process received characters
	tty.c_lflag &= ~ICANON;

	// Echo, erasure, new-line echo off.  Geoff doesn't thik these matter but...
	tty.c_lflag &= ~ECHO; // Disable echo
	tty.c_lflag &= ~ECHOE; // Disable erasure
	tty.c_lflag &= ~ECHONL; // Disable new-line echo

	// Signal characters, don't interpret INTR, QUIT, or SUSP characters if received
	tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP

	// Input modes  (c_iflag)  Input processing control
	// Disable IXON, IXOFF, IXANY  software flow control
	tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl

	// Disable any special handling of received bytes.  We just want raw data
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes


	// Output Modes  (c_oflag)
	// low level settings for output character processing.  Disable all
	tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
	tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
	// tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT IN LINUX)
	// tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT IN LINUX)

	// VMIN and VTIME affect how read() operates.  Both can be 0 or a positive number
	// Operation is unblocking read so VMIN and VTIME are 0.  read() returns
	// immediately with 0 or more bytes of data
	// Chosen mode here eliminates the time component since the 1022 never really
	// goes quiet.
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;

	// Baud Rate, set in speed and out speed
	cfsetispeed(&tty, B9600);
	cfsetospeed(&tty, B9600);
	
	// Some Linux implamentations let you set both at the same time
	// cfsetspeed(&tty, B9600);

	// Having updated termios let's save it
	if (tcsetattr(*serial_port, TCSANOW, &tty) != 0) {
		printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}


//--------------------------------------------------------------------
// unique_filename()
//--------------------------------------------------------------------
int unique_filename( char *base, char *name_out, int name_sz )
{
	// Base file name
	size_t base_len;
	strcat( name_out, base );
	base_len = strlen( base );
	
	// Get local time in preparation for formatting
	struct tm *tm;
	time_t t = time(NULL);
	tm = localtime( &t );
	if( tm == NULL ) {
		printf("localtime call FAILED \n");
		return 1;
	}

	// date and time encoding is as follows:
	// 2024-08-19-hh-mm-ss
	// %Y   %m %d %H %M %S
	// with dash separators:
	//s = strftime( name_out + base_len, name_sz - base_len, "%Y-%m-%d-%H-%M-%S", tm );
	
	// no dash separators:
	size_t s = strftime( name_out + base_len, name_sz - base_len, "%Y%m%d%H%M%S", tm );
	
	if( s ) {
		return 0;
	} else {
		return 1;
	}
}

#if 0
	// Sample call (Client Code)
	int rv;
	char new_file[100];
	new_file[0] = '\0';
	char base[] = "report-";
	rv = unique_filename( base, new_file, 100 );
	if( !rv ) {
		printf("New File: %s\n", new_file);
		f_hist = fopen( new_file, "w" );		
	} else {
		printf("unique_filename call FAILED \n");		
	}

#endif

//--------------------------------------------------------------------
//  control_receive_msg()
//  returns:
//       0  continue execution
//      -1  exit
//--------------------------------------------------------------------
int control_receive_msg( unsigned int *p_control )
{
	int rv = 0;
    client_req c_msg;
    server_rsp s_msg;
	ssize_t msg_len;

	memset( &c_msg, 0, sizeof(c_msg) );
	msg_len = msg_rcv_from_client( &c_msg );
	if( msg_len == 0 ) {
		// no messages in queue, do nothing.  This path most frequently taken
		return rv;
	}
	else if( msg_len == -1 ) {
		perror("msgrcv");
		return rv;
	}

	// Process a received message
	switch( c_msg.mtype )
	{
	case CLIENT_INIT:
		printf("Client Init received\n");
		//client_mq = c_msg.client_id;
		//printf("msglen %ld\n", msg_len);
		memset( &s_msg, 0, sizeof(s_msg) );
		s_msg.mtype = SERVER_ACTION_SUCCESS;
		sprintf( s_msg.rsp, "logmode %d", status_is_logmode() );
		if( msg_send_to_client(&s_msg) == -1 ) {
			perror("msgsnd");
		}
		break;
	case CLIENT_REQ_HISTORY:
		printf("Client History Request received\n");
		*p_control |= HISTORY_REQ;
		*p_control |= MESSAGE_SRC;
		memset( &s_msg, 0, sizeof(s_msg) );
		s_msg.mtype = SERVER_REQUEST_SUCCESS;
		strcpy( s_msg.rsp, "history");
		if( msg_send_to_client(&s_msg) == -1 ) {
			perror("msgsnd");
		}
		break;
	case CLIENT_REQ_LOG:
		printf("Client Log Toggle Request received\n");
		if( status_is_logmode() ) {
			*p_control |= LOGMODE_OFF_REQ;
		} else {
			*p_control |= LOGMODE_ON_REQ;
		}
		*p_control |= MESSAGE_SRC;		

		// When LOGMODE_OFF_REQ or LOGMODE_ON_REQ are accepted and
		// acted upon then SERVER_ACTION_SUCCESS response will reply to
		// the client with the new value
		memset( &s_msg, 0, sizeof(s_msg) );
		s_msg.mtype = SERVER_REQUEST_SUCCESS;
		s_msg.rsp[0] = '\0';
		if(msg_send_to_client(&s_msg) == -1 ) {
			perror("msgsnd");
		}
		break;
	case CLIENT_REQ_REPORT:
		printf("Client Report Request received\n");
		*p_control |= REPORT_REQ;
		*p_control |= MESSAGE_SRC;
		memset( &s_msg, 0, sizeof(s_msg) );
		s_msg.mtype = SERVER_REQUEST_SUCCESS;
		strcpy( s_msg.rsp, "report");
		if( msg_send_to_client(&s_msg) == -1 ) {
			perror("msgsnd");
		}
		break;
	case CLIENT_REQ_EXIT:
		printf("Client Exit Request received\n");
		memset( &s_msg, 0, sizeof(s_msg) );
		s_msg.mtype = SERVER_REQUEST_SUCCESS;
		strcpy( s_msg.rsp, "exit");
		if(msg_send_to_client(&s_msg) == -1 ) {
			perror("msgsnd");
		}
		rv = -1;
		break;
	}
	return rv;
}
