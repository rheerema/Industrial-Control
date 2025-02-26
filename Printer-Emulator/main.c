
//--------------------------------------------------------------------
// Printer Emulator
//
// This is the protocol engine component of the Diverter Dashboard Monitor
// project.  It provides the capability to connect to the 1022 RS-485
// communication link and engage in protocol transactions.  This allows it
// to request configuration reports, extract history logs, switch log mode
// on and off as well as capturing on-going refractometer readings being
// sent to the display module.
//
// The Printer Emulator requires a USB to RS-485 adapter.  The adaptor must
// be based on a chip that setserial is able to place into low latency mode.
//--------------------------------------------------------------------

#include "stdio.h"
#include "string.h"

// Linux headers
#include <stdlib.h>       // system()
#include <signal.h>       // signals
#include <sys/time.h>     // setpriority()
#include <sys/resource.h> // setpriority()
#include <sys/stat.h>     // stat()
#include <unistd.h>
#include <ctype.h>        // isalpha()

#include "parser.h"
#include "utils.h"
#include "../Common/message_services.h"

// Version String
const char version_stg[] = {"v1.3.1"};

//--------------------------------------------------------------------
// Unit Test Files
//--------------------------------------------------------------------
const char * test_file_arr[] = {
	"./Captures/A7-ll-1022-init-R-then-H.txt",                    // 0
	"./Captures/A7-1022-init-then-steady-state.txt",              // 1
	"./Captures/A7-1022-report-sequence.txt",                     // 2
	"./Captures/parse-header-report-req-from-1022-5-23-24.txt",   // 3
	"./Captures/A7-1022-history-sequence.txt",                    // 4
	"./Captures/parse-header-hist-req-5-23-24.txt",               // 5
	"./Captures/A7-1022-init-R-then-H.txt",                       // 6
	"./Captures/1022-startup-then-pm-start.txt",                  // 7
	"./Captures/history-active-try-3-10-15-24.txt",               // 8
	"./Captures/log-mode-on-evts-off.txt",                        // 9
	"./Captures/log-mode-prt-on-evts-prt-off.txt",                // 10
	"./Captures/A7-1022-logmode-on-req-report-logmode-off.txt",   // 11
	"./Captures/A7-1022-logmode-on-req-history-logmode-off.txt",  // 12
};

//--------------------------------------------------------------------
// Command Line Options Help Screen
//--------------------------------------------------------------------
const char * help_arr[] = {
	"  Printer Emulator for 1022 Diverter System",
	"  Runs in active mode with serial port in low latency when no arguments are passed",
	"  Optional Arguments",
	"    -c <file>  capture data on the wire to a file for testing",
	"    -d  is debug dump of parser state machine transitions (default: off)",
	"    -h  display this help screen",
	"    -p  is passive mode, act as listener between real printer module and 1022 (default: active)",
	"    -s  is \"slow\" high latency mode for serial port (default: low latency)",
	"    -u <idx>  run unit test with capture file at index <idx> (default: live data from serial port)",
	"\n"
	"  Run interactively",
	"      printem",
	"  Run a unit test",
	"      printem -d -p -s -u <testfile_idx>",
	"  Capture data on the wire",
	"      printem, -s -c <capfile>",
	"  Passively parse what's on the wire to stdout",
	"      printem -d -p",
};

//--------------------------------------------------------------------
// Globals and Definitions for main()
//--------------------------------------------------------------------
// Allocate memory for read buffer, set size according to your needs
unsigned char read_buf [256];

// Chunk Value is how many bytes in a line causes that line to be chunked
// with a subsequent line or lines.  This goes on until a line is encountered
// with less than ChunkVal bytes
enum ChunkVal {
	CHUNK_VAL_8 = 8,
	CHUNK_VAL_10 = 10,
	CHUNK_VAL_15 = 15,  // no chunking
};

// Serial Port name
char default_serial_port[] = "/dev/ttyUSB0";

// Test files
char capfile[128];      // capture file
char testfile[128];     // unit test file through -u

// Unit Test file index
int ut_idx;

// Trigers for testing with SIGUSR1 and SIGUSR2
int trigger1 = 0;
int trigger2 = 0;

// Directory for storing report, history, and log data files
// depends on execution environment target or desktop
const char desktop_dir[]    = { DESKTOP_DISK_DIR };
const char target_dir[]     = { TARGET_DISK_DIR };
const char target_ram_dir[] = { TARGET_RAM_DIR };
#define TARGET_STG "rpi"

#define DIR_PERMS (S_IRWXU | S_IRWXG | S_IRWXO)

// --- Signal Handlers ---
// Only these functions can be called inside a signal handler:
// https://man7.org/linux/man-pages/man7/signal-safety.7.html

//--------------------------------------------------------------------
// SIGINT handler
//--------------------------------------------------------------------
void INThandler( int sig )
{
	write(STDOUT_FILENO, "\nCtrl-C received\n", 17);

	// asynch thread safe _exit will terminate the calling process
	// immediately.  It will close any open file descriptors belonging
	// to the process
	_exit( 0 );
}

//--------------------------------------------------------------------
// SIGUSR1 handler
//     Used for testing to trigger a Report or History sequence
//     To send the signal from a shell:    kill -USR1 <pid>
//--------------------------------------------------------------------
void USR1handler( int sig )
{
	//write(STDOUT_FILENO, "\nUSR1 received\n", 15);
	trigger1 = 1;
	return;
}

//--------------------------------------------------------------------
// SIGUSR2 handler
//     Used for testing to trigger a Report or History sequence
//     To send the signal from a shell:    kill -USR2 <pid>
//--------------------------------------------------------------------
void USR2handler( int sig )
{
	//write(STDOUT_FILENO, "\nUSR1 received\n", 15);
	trigger2 = 1;
	return;
}

//--------------------------------------------------------------------
// main()
//--------------------------------------------------------------------
int main( int argc, char *argv[] )
{
	int rv;
	int serial_port;
	unsigned int options = ACTIVE_MODE | LOW_LATENCY;
	unsigned int control = 0;
	int isRunning = 1;
	int c;

	printf("\nPrinter Module Emulator %s\n", version_stg);
	printf("(c) 2025 Liquid Solids Control\n\n");
	
	// --- Start by processing command line options ---
	while( (c = getopt(argc, argv, "c:dhpsu:")) != -1 )
	{
		switch( c ) {
		case 'c':
			options |= CAPTURE;
			options &= ~ACTIVE_MODE;  // make parser take passive code path
				
			strcpy( capfile, optarg );
			printf( "Capture data on the wire and write to file %s\n", capfile );
			break;
		case 'd':
			options |= DEBUG_DUMP;
			printf( "Debug Dump activated\n");
						printf("options = 0x%x\n", options);
			break;
		case 'h':
			for(int i = 0; i < sizeof(help_arr) / sizeof(char *); ++i) {
				printf("%s\n", help_arr[i]);
			}
			return EXIT_SUCCESS;
			break;
		case 'p':
			options &= ~ACTIVE_MODE;
			printf("Passive Mode activated\n");
			printf("options = 0x%x\n", options);
			break;
		case 's':
			options &= ~LOW_LATENCY;
			printf( "Slow / High Latency serial port activated\n");
			printf("options = 0x%x\n", options);
			break;
		case 'u':
			options |= UNIT_TEST;
			if( isdigit( optarg[0] ) )
			{
				ut_idx = atoi(optarg);
				if( ut_idx < sizeof(test_file_arr) / sizeof(char *) ) {
					strcpy( testfile, test_file_arr[ut_idx] );
					printf( "Run unit test with file %s\n", testfile );
				} else {
					printf("unit test index is out of bounds\n");
					return EXIT_FAILURE;
				}
			}
			else
			{
				strcpy( testfile, optarg );
				printf( "Run unit test with file %s\n", testfile );
			}
			break;
		default:
		case '?':
			printf( "Unrecognized option encountered -%c\n", optopt );
			return EXIT_FAILURE;
			break;
		}
	}

	// --- Do some sanity checking on options passed by the user ---
	if( !(options & UNIT_TEST) && !(options & CAPTURE) && (options & ACTIVE_MODE) ) {
		printf("Printer Emulator is Running Interactively\n");
	}
	else if(   !(options & UNIT_TEST) && !(options & CAPTURE)
			 && (options & DEBUG_DUMP) && !(options & ACTIVE_MODE) ) {
		printf("Printer Emulator is Running Passively with Dump to Stdout\n");
	}
	else if(     (options & UNIT_TEST) && !(options & CAPTURE)
			 && (options & DEBUG_DUMP) && !(options & ACTIVE_MODE) ) {
		printf("Printer Emulator is Running Unit Test %s\n", testfile);
	}
	else if( !(options & UNIT_TEST) && (options & CAPTURE) && !(options & LOW_LATENCY) ) {
		printf("Printer Emulator is Capturing Data to %s\n", capfile );
	}
	else
	{
		// Error
		printf("Printer Emulaor does not support the given options.  Help:\n\n");
		for(int i = 0; i < sizeof(help_arr) / sizeof(char *); ++i) {
			printf("%s\n", help_arr[i]);
		}
		return EXIT_FAILURE;
	}

	if( options & ACTIVE_MODE) {
		printf("- printem is actively responding to 1022\n");
	} else {
		printf("- printem is passively monitoryng 1022 and printer module\n");
	}
	
	if( options & LOW_LATENCY) {
		printf("- serial port set for low latency\n");
	} else {
		printf("- serial port set for regular latency\n");
	}

	if( options & DEBUG_DUMP) {
		printf("- debug dump is On\n");
	} else {
		printf("- debug dump is OFF\n");
	}

	// Test if we're running on Target Hardware or a desktop
	FILE *sys_fp;
	char uname_stg[256];

	sys_fp = popen("uname -a", "r");
	if( sys_fp == NULL ) {
		perror("OS type check failed");
		return EXIT_FAILURE;
	}
	fgets( uname_stg, sizeof(uname_stg), sys_fp );
	
	char *token_p;
	const char *data_base_dir;
	token_p = strstr(uname_stg, TARGET_STG);
	if( token_p != NULL   ) {
		printf( "Target is execution environment (%s)\n", TARGET_STG );
		options |= TARGET;
		data_base_dir = target_dir;
	} else {
		printf( "Desktop is execution environment\n" );
		data_base_dir = desktop_dir;
	}
	pclose( sys_fp );

	// Create disk directory for target or desktop environment.
	// Test if disk directory already exists, create if not
	struct stat sb;  // stat buffer
	if( (stat(data_base_dir, &sb) == 0) && S_ISDIR(sb.st_mode)  ) {
		printf("Disk directory %s exists\n", data_base_dir );
	} else {
		// Create Data directory
		if( mkdir( data_base_dir, DIR_PERMS) == -1 ) {
			printf("UNABLE to create Disk directory %s\n", data_base_dir );
			return EXIT_FAILURE;
		} else {
			printf("Disk directory %s is created\n", data_base_dir);
		}
	}

	if( options & TARGET )
	{
		// On the target test if the RAM directory already exists, create if not
		if( (stat(target_ram_dir, &sb) == 0) && S_ISDIR(sb.st_mode)  ) {
			printf("RAM directory %s exists\n", target_ram_dir );
		} else {
			// Create RAM directory
			if( mkdir( target_ram_dir, DIR_PERMS) == -1 ) {
				printf("UNABLE to create RAM directory %s\n", target_ram_dir );
				return EXIT_FAILURE;
			} else {
				printf("RAM directory %s is created\n", target_ram_dir);
			}
		}
	}

	// nice values range -20 .. 0 .. +20  (highest priority, middle, lowest priority)
	// Make our process priority a little higher than most (-5)
	// This assumes the program is running under sudo...	
	setpriority( PRIO_PROCESS, 0, -5);

	// Install a signal handler to clean up gracefully upon Ctrl-C
	signal(SIGINT, INThandler);

	// Install for testing
	signal(SIGUSR1, USR1handler);
	signal(SIGUSR2, USR2handler);
	
	rv = serial_port_open( &serial_port, default_serial_port );
	if( EXIT_FAILURE == rv )
	{
		printf("Unable to open serial port %s\n", default_serial_port );
		return EXIT_FAILURE;
	}

	if( options & LOW_LATENCY )
	{
		// Update the serial port characteristic to low latency mode
		char tmp_cmd[512];
		sprintf( tmp_cmd, "setserial %s low_latency", default_serial_port );
		if( 0 == system( tmp_cmd ) ) {
			printf( "Serial port %s set for low_latency\n", default_serial_port );
		} else {
			printf( "Serial port %s UNABLE TO BE SET for low_latency\n", default_serial_port );
		}
	}

	parse_open( &options, &control, &serial_port );

	// --- Unit Test Mode ---
	if(      (options & UNIT_TEST) && !(options & CAPTURE)
		 && (options & DEBUG_DUMP) && !(options & ACTIVE_MODE) )
	{
		FILE *in_fp;
		char *in_line = NULL;
		size_t in_len = 0;
		ssize_t in_read;
		unsigned char in_data[16];
		int in_is_chunked;
		unsigned char in_chunked_data[16*6];  // assume 6 lines max
		unsigned char *in_chunked_p;

		in_fp = fopen( testfile, "r" );
		if( NULL == in_fp ) {
			printf("FAILED to open capture file");
			return EXIT_FAILURE;
		}
		
		in_chunked_p = in_chunked_data;
		in_is_chunked = 0;
		
		while ((in_read = getline(&in_line, &in_len, in_fp)) != -1)
		{
			if( in_line[0] == '-' )  continue;

			// Print the line sourcing data
			//printf("// %s", in_line);

			memset(in_data, 0, sizeof(in_data));
			
			// Apply scanf to line in format based on dump code from Attempt-7
			// and related files.  Note the extra space between first bank of 8
			// and second bank of 8.
			sscanf( in_line, "%2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx  %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx %2hhx",
					&in_data[0], &in_data[1], &in_data[2], &in_data[3],
					&in_data[4], &in_data[5], &in_data[6], &in_data[7],
					&in_data[8], &in_data[9], &in_data[10], &in_data[11],
					&in_data[12], &in_data[13], &in_data[14], &in_data[15] );

			for( int i = 0; i < 16; i++ )
			{
				if( in_data[i] ) {
					//printf("%hhx ", in_data[i] );
					*in_chunked_p = in_data[i];
					++in_chunked_p;
				}
				
				// Process if we've encountered 00 data somewhere before 16 bytes have
				// been scanned or if there are 16 bytes of data
				if( !in_data[i] || ( (i == 15) && in_data[i]) )
				{
					// We've reached the end of data for this segment.  Decide
					// what to do next
					if( in_is_chunked == 0)
					{
						if( i >= CHUNK_VAL_15 )
						{
							in_is_chunked = 1;
							// leave in_chunked_p where it is
						}
						else
						{
							// we do stuff with the data here
							//DumpHexStdout( (const void*)in_chunked_data, i );
							parse_header(i, in_chunked_data);
							
							in_chunked_p = in_chunked_data;
						}
						break;
					}
					
					if( in_is_chunked && (i < CHUNK_VAL_15) )
					{
						// We do stuff with the data here
						//DumpHexStdout( (const void*)in_chunked_data, in_chunked_p - in_chunked_data );
						parse_header(in_chunked_p - in_chunked_data, in_chunked_data);
	 
						in_is_chunked = 0;
						in_chunked_p = in_chunked_data;
					}
					break;
				}
			}
		
			// separate input line and scanned line pairs
			//printf("\n");  // was two
		}

		// close the input file
		fclose(in_fp);
	}  // END if( options & UNIT_TEST )

	// --- Capture Wireline Data to a File ---
	if( !(options & UNIT_TEST) && (options & CAPTURE) && !(options & LOW_LATENCY) )
	{
		FILE *cap_fp;
		cap_fp = fopen( capfile, "w");
			
		while( 1 )
		{
			// Read bytes in blocking mode (see VMIN and VTIME)
			int n = read(serial_port, &read_buf, sizeof(read_buf));

			// Not parsing, just dumping
			//header_parse(n, read_buf, serial_port);

			if( n )
			{
				// Print a splat to console to show something is happening
				printf("* ");
				fprintf( cap_fp, "--- Datagram ---\n");
				DumpHex( (const void*)read_buf, n, cap_fp );
			}
			
			// read() is blocking so no sleep needed
			//usleep(10*1000);
		}

		// close the capture file
		fclose( cap_fp );
	}
	
	// --- Serial Port Parsing of Live Wireline Data ---
	if( !(options & UNIT_TEST) && !(options & CAPTURE) && (options & ACTIVE_MODE) )
	{
		rv = msg_create_server_mq();
		if( rv == -1 ) {
			perror("msgget");
			parse_close();
			close(serial_port);
			exit(EXIT_FAILURE);
		}
		while( isRunning )
		{
			// Test for trigger of Report or History sequence.
			// Comes in from USR1 signal
			if( trigger1 ) {
				trigger1 = 0;
				control |= LOGMODE_ON_REQ;
			}
			if( trigger2 ) {
				trigger2 = 0;
				control |= LOGMODE_OFF_REQ;
			}

			//printf("* ");  fflush(stdout);

			// Read the control message queue for requests
			// This is non-blocking
			rv = control_receive_msg( &control );
			if( rv == -1 ) {
				isRunning = 0;
			}
			
			// Read bytes in blocking mode (see VMIN and VTIME)
			int n = read(serial_port, &read_buf, sizeof(read_buf));

			parse_header(n, read_buf);

			// read() is blocking so no sleep needed
			// Sleep 10 mS to let other threads run
			// 1 mS is 1000 uS
			//usleep(10*1000);
		}

		// Remove the server message queue
		rv = msg_remove_server_mq();
		if( rv == -1 ) {
			perror("msgctl");
		} else {
			printf("Server message queue removed\n");
		}
	}

	parse_close();
	
	// close the port
	close(serial_port);

    return EXIT_SUCCESS;
}

