
//--------------------------------------------------------------------
//  Parser for 1022 RS-485 Protocol
//--------------------------------------------------------------------

#include "stdio.h"
#include <unistd.h>   // write(), read(), close(), usleep()
#include <stdlib.h>   // system()
#include <string.h>   // memset()
#include <time.h>     // difftime()

#include "parser.h"
#include "utils.h"
#include "../Common/message_services.h"

//--------------------------------------------------------------------
// State Machine Globals
//--------------------------------------------------------------------
unsigned char buffer[256];
int           buffer_len;
unsigned char tx_buf[16];
unsigned int *p_options;      // static
int          *p_port;         // static
unsigned int *p_control;      // dynamic
state_t       header_state;
unsigned char status;         // printer module status
int           hst_is_first;   // History first request

// Refractometer reading snapshot control
time_t snapshot_now, snapshot_interval;
int    is_snapshot;

// File handles
FILE *        f_out;  // parser output to a file
FILE *        f_rpt;  // Report data to a file (opened on demand)
FILE *        f_hst;  // History data to a file (opened on demand)
FILE *        f_log;  // Log data to a file (opened on demand)
FILE *        f_rdg;  // Refractometer current readings

char curr_report_file[DATA_FILENAME_SIZE];
char curr_history_file[DATA_FILENAME_SIZE];
char curr_log_file[DATA_FILENAME_SIZE];

//--------------------------------------------------------------------
//  DumpHexStdout()
//--------------------------------------------------------------------
void DumpHexStdout(const void* data, size_t size) {
	char ascii[17];
	size_t i, j;
	ascii[16] = '\0';
	for (i = 0; i < size; ++i) {
		printf("%02X ", ((unsigned char*)data)[i]);
		if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') {
			ascii[i % 16] = ((unsigned char*)data)[i];
		} else {
			ascii[i % 16] = '.';
		}
		if ((i+1) % 8 == 0 || i+1 == size) {
			printf(" ");
			if ((i+1) % 16 == 0) {
				printf("|  %s \n", ascii);
			} else if (i+1 == size) {
				ascii[(i+1) % 16] = '\0';
				if ((i+1) % 16 <= 8) {
					printf(" ");
				}
				for (j = (i+1) % 16; j < 16; ++j) {
					printf("   ");
				}
				printf("|  %s \n", ascii);
			}
		}
	}
}

//--------------------------------------------------------------------
// status_get()
//--------------------------------------------------------------------
unsigned char status_get()
{
	return status;
}

//--------------------------------------------------------------------
// status_set_logmode()
//--------------------------------------------------------------------
void status_set_logmode()
{
	status |= ST_LOGMODE;
}

//--------------------------------------------------------------------
// status_clr_logmode()
//--------------------------------------------------------------------
void status_clr_logmode()
{
	status &= ~ST_LOGMODE;
}

//--------------------------------------------------------------------
// status_is_logmode()
//--------------------------------------------------------------------
int status_is_logmode()
{
	return status & ST_LOGMODE;
}

//--------------------------------------------------------------------
// parse_open()
//--------------------------------------------------------------------
void parse_open( unsigned int *options, unsigned int *control, int *port )
{
	// Save a pointer to system options
	p_options = options;

	// Save a pointer to the control register
	p_control = control;
	
	// save a pointer to the serial port
	p_port = port;

	// printer status wakes up happy and ready to go
	// Could OR-in ST_LOGMODE here if we want to wake up that way
	status = ST_PRWON | ST_READY;
	
	// State Macine init
	header_state = SS_UNKNOWN;
	buffer_len = 0;

	char path_stg[DATA_FILENAME_SIZE];
	path_stg[0] = '\0';
	if( *p_options & TARGET ) {
		strcat( path_stg, TARGET_RAM_DIR );
		strcat( path_stg, "/readings.txt" );
	} else {
		strcat( path_stg, DESKTOP_DISK_DIR );
		strcat( path_stg, "/readings.txt" );
	}
	f_rdg = fopen( path_stg, "w");
	
	//f_out = fopen("logfile.txt", "w");
	f_out = NULL;
	f_rpt = NULL;
	f_hst = NULL;
	f_log = NULL;

	// Start the snapshot timer for refractometer readings.  This
	// limits how often the file "readings.txt" is updated.
	snapshot_interval = time( NULL );
	is_snapshot = 0;
}

//--------------------------------------------------------------------
// parse_close()
//--------------------------------------------------------------------
void parse_close()
{
	// close all open files
	if( NULL != f_rdg ) {
		fclose( f_rdg);
	}
	if( NULL != f_out ) {
		fclose( f_out);
	}
	if( NULL != f_rpt ) {
		fclose( f_rpt);
	}
	if( NULL != f_hst ) {
		fclose( f_hst);
	}
	if( NULL != f_log ) {
		fclose( f_log);
	}
}

//--------------------------------------------------------------------
// SS_Unknown
//--------------------------------------------------------------------
void SS_Unknown(int i, unsigned char *data)
{
	if( data[i] == 0x91 )
	{
		// We've just seen the end of a segment of data
		// Discard the 0x98 at the end of the buffer
		//--buffer_len;
		
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Unknown ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
			
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = 0x91;
		header_state = SS_DISPLAY;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To SS Display ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}				
}

//--------------------------------------------------------------------
// SS_Pause
//--------------------------------------------------------------------
void SS_Pause(int i, unsigned char *data)
{
	if( data[i] == 0x90 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Pause ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// Here we check for pending Report or History requests if we are
		// in Active mode.  If not we just respond with status.  If we're
		// in Passive mode just buffer characters.
		
		if( *p_options & ACTIVE_MODE )
		{
			// Handle all the Active cases.  These are pulled into a
			// separate function to cut down on code clutter.
			SS_Pause_Active( i, data );
		}
		else
		{
			// Passive mode.  Reset to receive the next
			buffer_len = 0;
			buffer[buffer_len++] = data[i];
			header_state = SS_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To SS Printer ---\n");
			}			
		}	
	}
	else if(    buffer_len >= 2
			&& (buffer[buffer_len - 2] == 0x40)    // '@' character
			&& (data[i] == 0x0d) )
	{
		// This is an @ directive from the 1022.  @R, @H, or @L
		// This is where the 1022 sends them to kick off a report, history
		// or logmode sequence.  buffer_len - 1 points to R, H, or L
		// Report, History, or Logmode (respectively)
		if( *p_options & ACTIVE_MODE )
		{
			switch( buffer[buffer_len - 1] )
			{
			case 0x52:    // @R for Report
				*p_control |= REPORT_REQ;
				break;
			case 0x48:    // @H for History
				*p_control |= HISTORY_REQ;
				break;
			case 0x4c:    // @l for Log Mode ON
				*p_control |= LOGMODE_ON_REQ;
				break;
			default:
				printf("--- SS Pause Error Invalid @%c ---\n", buffer[buffer_len - 1]);
				break;
			}
		}
		
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Pause @%c Cmd ---\n", buffer[buffer_len - 1] );
			buffer[buffer_len++] = data[i];
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
		
		// Take action and reset buffer.  Remain in SS_PAUSE
		buffer_len = 0;
	}
	else
	{
#if 0
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Pause Error ---");
		}
#endif
		
		// Accumulate characters sent by the 1022
		buffer[buffer_len] = data[i];
		++buffer_len;				
	}
}

//--------------------------------------------------------------------
// SS_Pause_Active
//     data[i-1] == 0x98
//     data[i]   == 0x90    This is Printer response code
//--------------------------------------------------------------------
void SS_Pause_Active(int i, unsigned char *data)
{
	server_rsp s_msg;
	
	if( *p_control & REPORT_REQ )
	{
		// Clear the condition then act on it
		*p_control &= ~REPORT_REQ;
		
		tx_buf[0] = status_get();
		tx_buf[1] = 0x52;  tx_buf[2] = 0x0D;
		write( *p_port, tx_buf, 3 );

		// Format the buffer accordingly
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		buffer[buffer_len++] = status_get();
		buffer[buffer_len++] = 0x52;    // R initiates a Report
		buffer[buffer_len++] = 0x0D;
		
		header_state = RPT_START;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To RPT Start ---\n");
		}
	}
	else if( *p_control & HISTORY_REQ )
	{
		// Clear the condition then act on it
		*p_control &= ~HISTORY_REQ;

		tx_buf[0] = status_get();
		tx_buf[1] = 0x49;  tx_buf[2] = 0x0D;
		write( *p_port, tx_buf, 3 );
		
		// Format the buffer accordingly
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		buffer[buffer_len++] = status_get();
		buffer[buffer_len++] = 0x49;    // I initiates History
		buffer[buffer_len++] = 0x0D;
		
		header_state = HST_START;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To HST Start ---\n");
		}
	}
	else if( *p_control & LOGMODE_ON_REQ )
	{
		// Clear the condition then act on it
		*p_control &= ~LOGMODE_ON_REQ;

		// Enable logmode status bit
		status_set_logmode();

		if( *p_control & MESSAGE_SRC ) {
			*p_control &= ~MESSAGE_SRC;

			// Notify the controlling client of success
			memset( &s_msg, 0, sizeof(s_msg) );
			s_msg.mtype = SERVER_ACTION_SUCCESS;
			sprintf(s_msg.rsp, "logmode 1" );
			if( msg_send_to_client(&s_msg) == -1 ) {
				perror("msgsnd");
			}
		}

		tx_buf[0] = status_get();    // 0x54 printer status
		tx_buf[1] = 0x54;            // 'T' starts log mode
		tx_buf[2] = 0x0D;           // CR
		write( *p_port, tx_buf, 3 );
		
		// Format the buffer accordingly
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		buffer[buffer_len++] = status_get();
		buffer[buffer_len++] = 0x54;    // 'T' starts log mode
		buffer[buffer_len++] = 0x0D;
		
		header_state = LOG_START;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To LOG Start ---\n");
		}			
	}
	else
	{
		// Steady State printer Module Queary Response
		// Send "printer ready" to 1022
		tx_buf[0] = status_get();
		write( *p_port, tx_buf, 1 );
		
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];    // 0x90
		// Record the status we just sent
		buffer[buffer_len++] = status_get();
		
		header_state = SS_PRINTER;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To SS Printer ---\n");
		}
	}	
}

//--------------------------------------------------------------------
// SS_Display
//--------------------------------------------------------------------
void SS_Display(int i, unsigned char *data)
{
	size_t cnt;
	
	if( data[i] == 0x98 )
	{
		// We've just seen the end of a segment of data
		// Discard the 0x98 at the end of the buffer
		//--buffer_len;
		
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		if( is_snapshot )
		{
			is_snapshot = 0;
			//printf("SS update readings.txt\n");
			//DumpHexStdout( (const void*)buffer, buffer_len );
			rewind(f_rdg);
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_rdg );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- SS Display Data Write Error ---\n");
			}
		}

		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = SS_PAUSE;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To SS Pause ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// SS_Printer
//--------------------------------------------------------------------
void SS_Printer(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- SS Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// Possibly go to SS_REPORT mode if that's in progrss?
						
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = SS_PAUSE;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To SS Pause ---\n");
		}
	}
	else
	{
		// If we're passively monitoring, look for 'R' Report sequence
		// initiation.  The previous byte will have been status (e.g. 0x44)
		// If we're emulating the printer then we begin the Report sequence
		// by sending 52 0D.  I think we would do that here.
		if( (buffer[buffer_len - 1] == 0x52) && (data[i] == 0x0d) ) {
			// Store off the 0x0d and prepare to receive data
			buffer[buffer_len] = data[i];
			++buffer_len;
			header_state = RPT_START;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To RPT Start ---\n");
			}
		}
		// If we're passively monitoring, look for 'I' History sequence start
		else if( (buffer[buffer_len - 1] == 0x49) && (data[i] == 0x0d) ) {
			// Store off the 0x0d and prepare to receive data
			buffer[buffer_len] = data[i];
			++buffer_len;
			header_state = HST_START;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To HST Start ---\n");
			}
		}
		// If we're passively monitoring, look for 'T' Logmode On sequence.
		// 'T' is the on start request.  Subsequent data is requested by 'L'
		else if( (buffer[buffer_len - 1] == 0x54) && (data[i] == 0x0d) ) {
			// Store off the 0x0d and prepare to receive data
			buffer[buffer_len] = data[i];
			++buffer_len;
			header_state = LOG_START;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To LOG Start ---\n");
			}
		}
		else
		{
			buffer[buffer_len] = data[i];
			++buffer_len;
		}
	}
}

//--------------------------------------------------------------------
// RPT_Start
//--------------------------------------------------------------------
void RPT_Start(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Rpt Start ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
#if 1
		// Open the Report file for writing
		char base_stg[DATA_FILENAME_SIZE];
		base_stg[0] = '\0';
		if( *p_options & TARGET )
		{
			// Target location for Report file
			strcat( base_stg, TARGET_RAM_DIR );
			strcat( base_stg, "/report.txt" );
			f_rpt = fopen( base_stg, "w");
			rewind( f_rpt );
			// Save path and filename to send to client
			curr_report_file[0] = '\0';
			strcpy( curr_report_file, base_stg );
		}
		else
		{
			// Desktop location for Report file
			int rv;
			curr_report_file[0] = '\0';
			strcat( base_stg, DESKTOP_DISK_DIR );
			strcat( base_stg, "/report-" );
			rv = unique_filename( base_stg, curr_report_file, DATA_FILENAME_SIZE );
			if( !rv ) {	
				f_rpt = fopen(curr_report_file, "w");
			} else {
				if( *p_options & DEBUG_DUMP ) {
					printf("unique_filename call (report) FAILED \n");
				}
			}
		}
#endif
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = RPT_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Rpt Data ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// RPT_Data
//--------------------------------------------------------------------
void RPT_Data(int i, unsigned char *data)
{
	size_t cnt;
	server_rsp s_msg;

	// Rpt Data keeps going until a 0x91 (VFD record) is encountered
	if( data[i] == 0x91 )
	{
		// Look for ";end" at the end of data representing exit to steady state
		if(     buffer_len >= 5
			&& (buffer[buffer_len - 5] == 0x3B)
			&& (buffer[buffer_len - 4] == 0x65)
			&& (buffer[buffer_len - 3] == 0x6E)
			&& (buffer[buffer_len - 2] == 0x64)
			&& (buffer[buffer_len - 1] == 0x0D) )
		{
			if( *p_options & DEBUG_DUMP ) {
				printf("--- Rpt Data Last ---\n");
				DumpHexStdout( (const void*)buffer, buffer_len );
			}

			// Write this record then close the report file
			if( NULL != f_rpt ) {
#if 0
				// FUTURE: suppress the leading 0x98 (needs investigating)
				unsigned char *temp_buffer = buffer;
				int temp_buffer_len = buffer_len;
				if( buffer[0] == 0x98 ) {
					temp_buffer += 1;
					temp_buffer_len -= 1;
				}
#endif
				// Write this record to the file
				cnt = fwrite( (void *)buffer, 1, buffer_len, f_rpt );
				if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
					printf("--- Rpt Data Write Error ---\n");
				}
				fclose( f_rpt );
				f_rpt = NULL;
			}

			if( *p_control & MESSAGE_SRC ) {
				*p_control &= ~MESSAGE_SRC;

				// Notify the controlling client of success
				memset( &s_msg, 0, sizeof(s_msg) );
				s_msg.mtype = SERVER_ACTION_SUCCESS;
				sprintf(s_msg.rsp, "report %s", curr_report_file );
				if( msg_send_to_client(&s_msg) == -1 ) {
					perror("msgsnd");
				}
			}

			if( status_is_logmode() )
			{
				buffer_len = 0;
				buffer[buffer_len++] = data[i];
				header_state = LOG_DISPLAY;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To  LOG Display ---\n");
				}
			}
			else
			{
				buffer_len = 0;
				buffer[buffer_len++] = data[i];
				header_state = SS_DISPLAY;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To  SS Display ---\n");
				}
			}
		}
		else
		{
			// This is just a regular 0x91 VFD data record
			if( *p_options & DEBUG_DUMP ) {
				printf("--- Rpt Data ---\n");
				DumpHexStdout( (const void*)buffer, buffer_len );
			}

			if( NULL != f_rpt ) {
#if 0
				// FUTURE: suppress the leading 0x98  (needs investigating)
				unsigned char *temp_buffer = buffer;
				int temp_buffer_len = buffer_len;
				if( buffer[0] == 0x98 ) {
					temp_buffer += 1;
					temp_buffer_len -= 1;
				}
#endif
				// Write this record to the file
				cnt = fwrite( (void *)buffer, 1, buffer_len, f_rpt );
				if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
					printf("--- Rpt Data Write Error ---\n");
				}
			}
			
			// Now reset to receive the next
			buffer_len = 0;
			buffer[buffer_len++] = data[i];
			header_state = RPT_DISPLAY;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Rpt Display ---\n");
			}
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// RPT_Display
//--------------------------------------------------------------------
void RPT_Display(int i, unsigned char *data)
{
	size_t cnt;
	
	// Display keeps going until a printer status request is received
	if( (buffer[buffer_len - 1] == 0x98) && (data[i] == 0x90) )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Rpt Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		if( is_snapshot )
		{
			is_snapshot = 0;
			//printf("RPT update readings.txt\n");
			//DumpHexStdout( (const void*)buffer, buffer_len );
			rewind(f_rdg);
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_rdg );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- RPT Display Data Write Error ---\n");
			}	
		}

		if( *p_options & ACTIVE_MODE )
		{
			// Active mode response: Send "printer ready" to 1022
			tx_buf[0] = status_get();
			write( *p_port, tx_buf, 1 );

			// Format the buffer accordingly
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			buffer[buffer_len++] = status_get();

			header_state = RPT_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Rpt Printer ---\n");
			}
		}
		else
		{
			// Passive mode: reset to receive any printer status
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			header_state = RPT_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Rpt Printer ---\n");
			}
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// RPT_Printer
//--------------------------------------------------------------------
void RPT_Printer(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Rpt Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
						
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = RPT_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Rpt Data ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// HST_Start
//--------------------------------------------------------------------
void HST_Start(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Start ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
#if 1
		// Open the History file for writing
		char base_stg[DATA_FILENAME_SIZE];
		base_stg[0] = '\0';
		if( *p_options & TARGET )
		{
			// Target location for History file
			strcat( base_stg, TARGET_RAM_DIR );
			strcat( base_stg, "/history.txt" );
			f_hst = fopen( base_stg, "w");
			rewind( f_hst );
			// Save path and filename to send to client
			curr_history_file[0] = '\0';
			strcpy( curr_history_file, base_stg );
		}
		else
		{
			// Desktop location for History file
			int rv;
			curr_history_file[0] = '\0';
			strcat( base_stg, DESKTOP_DISK_DIR );
			strcat( base_stg, "/history-" );
			rv = unique_filename( base_stg, curr_history_file, DATA_FILENAME_SIZE );
			if( !rv ) {	
				f_hst = fopen(curr_history_file, "w");
			} else {
				if( *p_options & DEBUG_DUMP ) {
					printf("unique_filename call (history) FAILED \n");
				}
			}
		}
#endif
		// First history request
		hst_is_first = 1;
		
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = HST_DISPLAY;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Hst Display ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// HST_Display
//--------------------------------------------------------------------
void HST_Display(int i, unsigned char *data)
{
	size_t cnt;
	
	if( (buffer[buffer_len - 1] == 0x98) && (data[i] == 0x90) )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		if( is_snapshot )
		{
			is_snapshot = 0;
			//printf("HST update readings.txt\n");
			//DumpHexStdout( (const void*)buffer, buffer_len );
			rewind(f_rdg);
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_rdg );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- HST Display Data Write Error ---\n");
			}
		}

		if( *p_options & ACTIVE_MODE )
		{
			if( hst_is_first )
			{
				hst_is_first = 0;
				// First Hst data request is 'T'
				tx_buf[0] = status_get();
				tx_buf[1] = 0x54;  tx_buf[2] = 0x0D;
				write( *p_port, tx_buf, 3 );
				
				// Format the buffer accordingly
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				buffer[buffer_len++] = 0x54;    // T requests first record
				buffer[buffer_len++] = 0x0D;
				
				header_state = HST_PRINTER_ACTIVE;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To Printer Active ---\n");
				}
			}
			else
			{
				// All subsequent Hst data requsts are 'H'
				tx_buf[0] = status_get();
				tx_buf[1] = 0x48;  tx_buf[2] = 0x0D;
				write( *p_port, tx_buf, 3 );
				
				// Format the buffer accordingly
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				buffer[buffer_len++] = 0x48;    // H requests subsequent record
				buffer[buffer_len++] = 0x0D;
				
				header_state = HST_PRINTER_ACTIVE;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To Hst Printer Active ---\n");
				}
			}
		}
		else
		{
			// Passive mode: reset to receive any printer status
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			header_state = HST_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Printer ---\n");
			}
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// HST_Printer    (Passive)
//--------------------------------------------------------------------
void HST_Printer(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
		
		// Check for 'H' type Printer record which means data follows
		// look at hist-parse-before-hst-changes.txt
		if(     (buffer[buffer_len - 2] == 0x48)    // 'H'
			&& (buffer[buffer_len - 1] == 0x0D))   //  <CR>
		{
			// This is a 'H' type Printer record
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			header_state = HST_DATA;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Data ---\n");
			}
		}
		// Check for 'T' type Printer record which means data follows
		// look at hist-parse-before-hst-changes.txt
		else if(     (buffer[buffer_len - 2] == 0x54)    // 'T'
				 && (buffer[buffer_len - 1] == 0x0D))   //  <CR>
		{
			// This is a 'T' type Printer record
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			header_state = HST_DATA;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Data ---\n");
			}
		}
		else
		{
			// This is an ordinary Printer record
			buffer_len = 0;
			buffer[buffer_len++] = data[i];
			header_state = HST_DISPLAY;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Display ---\n");
			}
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// HST_Printer_Active
//     When state machine sends 44 54 0D ('T') or 44 48 0D ('H') requests to
//     the 1022 it will get a data record in response.  We come to this handler
//     which always terminates with Hst Data
//     we come to this handler.  It is always terminated by LOG_Data.
//--------------------------------------------------------------------
void HST_Printer_Active(int i, unsigned char *data)
{
	// Terminate at beginning of Hst Data
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// Now reset to move on to Hst Data
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = HST_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Hst Data ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// HST_Data
//--------------------------------------------------------------------
void HST_Data(int i, unsigned char *data)
{
	size_t cnt;
	server_rsp s_msg;

#if 0
	if( (data[i] == 0x90) && (buffer[buffer_len - 1] == 0x98) )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Data ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
		
		if( NULL != f_hst ) {
			// Write this record to the file
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_hst );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- Hst Data Write Error ---\n");
			}
		}

		if( *p_options & ACTIVE_MODE )
		{
			// We just received a Hst Data record.  Now request the
			// next via H record and induce printer to return here
			tx_buf[0] = status_get();
			tx_buf[1] = 0x48;  tx_buf[2] = 0x0D;
			write( *p_port, tx_buf, 3 );
			
			// Format the buffer accordingly
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			buffer[buffer_len++] = status_get();
			buffer[buffer_len++] = 0x48;    // H requests subsequent record
			buffer[buffer_len++] = 0x0D;
			
			header_state = HST_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Printer ---\n");
			}
		}
		else
		{
			// Passive: Reset to receive printer status
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			header_state = HST_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Printer ---\n");
			}
		}
	}
#endif
	
#if 1
	// Display record terminates Hst Data
	if( data[i] == 0x91 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Hst Data ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		if( NULL != f_hst ) {
			// Write this record to the file
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_hst );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- Hst Data Write Error ---\n");
			}
		}

		// If we have received ";end" then we return to SS operation
		if(	   (buffer[buffer_len - 5] == 0x3B)
			&& (buffer[buffer_len - 4] == 0x65)
			&& (buffer[buffer_len - 3] == 0x6E)
			&& (buffer[buffer_len - 2] == 0x64)
			&& (buffer[buffer_len - 1] == 0x0D) )
		{
			// We've written the record now close the file
			fclose( f_hst );
			f_hst = NULL;

			if( *p_control & MESSAGE_SRC ) {
				*p_control &= ~MESSAGE_SRC;

				// Notify the controlling client of success
				memset( &s_msg, 0, sizeof(s_msg) );
				s_msg.mtype = SERVER_ACTION_SUCCESS;
				sprintf(s_msg.rsp, "history %s", curr_history_file );
				if( msg_send_to_client(&s_msg) == -1 ) {
					perror("msgsnd");
				}
			}
			
			if( status_is_logmode() )
			{
				buffer_len = 0;
				buffer[buffer_len++] = data[i];
				header_state = LOG_DISPLAY;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To LOG Display ---\n");
				}
			}
			else
			{
				// Now return to Steady State operation
				buffer_len = 0;
				buffer[buffer_len++] = data[i];
				header_state = SS_DISPLAY;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To SS Display ---\n");
				}
			}
		}
		else
		{
			// Now reset to receive any display data
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			header_state = HST_DISPLAY;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Hst Display ---\n");
			}
		}
	}
#endif
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// LOG_Start
//--------------------------------------------------------------------
void LOG_Start(int i, unsigned char *data)
{
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Start ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
#if 1
		// Log file location depends on TARGET or not
		int rv;
		curr_log_file[0] = '\0';
		char base_stg[DATA_FILENAME_SIZE];
		base_stg[0] = '\0';
		if( *p_options & TARGET )
		{
			strcat( base_stg, TARGET_DISK_DIR );
		} else {
			strcat( base_stg, DESKTOP_DISK_DIR );
		}
		strcat( base_stg, "/logmode-" );
		rv = unique_filename( base_stg, curr_log_file, DATA_FILENAME_SIZE );
		if( !rv ) {	
			f_log = fopen(curr_log_file, "w");
		} else {
			if( *p_options & DEBUG_DUMP ) {
				printf("unique_filename call (logmode) FAILED \n");
			}
		}
#endif
		// Set Log mode bit in status so that Report or History sequences
		// return to Log mode when complete.  Covers the passive case.
		status_set_logmode();

		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = LOG_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Log Data ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// LOG_Data
//--------------------------------------------------------------------
void LOG_Data(int i, unsigned char *data)
{
	size_t cnt;
	int temp_buffer_len;
	unsigned char *temp_buffer;
		
	if( data[i] == 0x91 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Data ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
		
		// If we're in active mode look for '@' signaling from the 1022.
		// Note that buffer[0] == 0x98
		if( (*p_options & ACTIVE_MODE) && (buffer[1] == 0x40 ) )
		{
			switch( buffer[2])
			{
			case 0x4c:
				// 1022 sent "@L" to request the end of Log mode
				*p_control |= LOGMODE_OFF_REQ;
				break;
			case 0x52:
				// 1022 sent "@R" to initiate a Report while in Log mode
				*p_control |= REPORT_REQ;
				break;
			case 0x48:
				// 1022 sent "@H" to initiate a History sequence while in log mode
				*p_control |= HISTORY_REQ;
				break;
			default:
				printf("--- Log Data Error: Invalid @%c ---\n", buffer[2]);
				break;
			}
		}

		// This is a regular Log data record
		// Write it to logfile if it is not a ";wait" string
		if( (NULL != f_log) && (buffer[1] != 0x3B) ) {
			// suppress the leading 0x98
			temp_buffer = buffer;
			temp_buffer_len = buffer_len;
			if( buffer[0] == 0x98 ) {
				temp_buffer += 1;
				temp_buffer_len -= 1;
			}
			// Write this record to the file
			cnt = fwrite( (void *)temp_buffer, 1, temp_buffer_len, f_log );
			if( (*p_options & DEBUG_DUMP) && (cnt != temp_buffer_len) ) {
				printf("--- Log Data Write Error ---\n");
			}
		}
		
		// Now reset to receive the next
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = LOG_DISPLAY;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Log Display ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}

//--------------------------------------------------------------------
// LOG_Display
//--------------------------------------------------------------------
void LOG_Display(int i, unsigned char *data)
{
	size_t cnt;
	server_rsp s_msg;
	
	// Display keeps going until a printer status request is received
	if( (buffer[buffer_len - 1] == 0x98) && (data[i] == 0x90) )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		if( is_snapshot )
		{
			is_snapshot = 0;
			//printf("LOG update readings.txt\n");
			//DumpHexStdout( (const void*)buffer, buffer_len );
			rewind(f_rdg);
			cnt = fwrite( (void *)buffer, 1, buffer_len, f_rdg );
			if( (*p_options & DEBUG_DUMP) && (cnt != buffer_len) ) {
				printf("--- LOG Display Data Write Error ---\n");
			}
		}

		if( *p_options & ACTIVE_MODE )
		{
			if( *p_control & LOGMODE_OFF_REQ )
			{
				*p_control &= ~LOGMODE_OFF_REQ;
				
				// We are exititing log mode
				status_clr_logmode();

				if( *p_control & MESSAGE_SRC ) {
					*p_control &= ~MESSAGE_SRC;
					
					// Notify the controlling client of success
					memset( &s_msg, 0, sizeof(s_msg) );
					s_msg.mtype = SERVER_ACTION_SUCCESS;
					sprintf(s_msg.rsp, "logmode 0 %s", curr_log_file );
					if( msg_send_to_client(&s_msg) == -1 ) {
						perror("msgsnd");
					}
				}
				
				tx_buf[0] = status_get();    // Send regular status (now 0x44 again)
				write( *p_port, tx_buf, 1 );

				// Close the logmode file
				if( f_log != NULL ) {
					fclose( f_log );
					f_log = NULL;
				}

				// Return to Steady State
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				header_state = SS_PRINTER;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To SS Printer ---\n");
				}
			}
			else if( *p_control & REPORT_REQ )
			{
				*p_control &= ~REPORT_REQ;
				
				tx_buf[0] = status_get();
				tx_buf[1] = 0x52;  tx_buf[2] = 0x0D;
				write( *p_port, tx_buf, 3 );

				// Format the buffer accordingly
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				buffer[buffer_len++] = 0x52;    // R initiates a Report
				buffer[buffer_len++] = 0x0D;
				
				header_state = RPT_START;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To RPT Start ---\n");
				}
			}
			else if( *p_control & HISTORY_REQ )
			{
				*p_control &= ~HISTORY_REQ;

				tx_buf[0] = status_get();
				tx_buf[1] = 0x49;  tx_buf[2] = 0x0D;
				write( *p_port, tx_buf, 3 );
				
				// Format the buffer accordingly
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				buffer[buffer_len++] = 0x49;    // I initiates History
				buffer[buffer_len++] = 0x0D;
				
				header_state = HST_START;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To HST Start ---\n");
				}
			}
			else
			{
				// Active mode requests next log mode data record
				tx_buf[0] = status_get();
				tx_buf[1] = 0x4C;             // 'L' request next record
				tx_buf[2] = 0x0D;            // CR
				write( *p_port, tx_buf, 3 );

				// Format the buffer accordingly
				buffer_len = 0;
				buffer[buffer_len++] = 0x98;
				buffer[buffer_len++] = data[i];
				buffer[buffer_len++] = status_get();
				buffer[buffer_len++] = 0x4C;;
				buffer[buffer_len++] = 0x0D;

				header_state = LOG_PRINTER_ACTIVE;
				if( *p_options & DEBUG_DUMP ) {
					printf("--- To Log Printer Active ---\n");
				}
			}
		}
		else
		{
			// Passive mode: reset to receive any printer status
			buffer_len = 0;
			buffer[buffer_len++] = 0x98;
			buffer[buffer_len++] = data[i];
			header_state = LOG_PRINTER;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To Log Printer ---\n");
			}
		}
	}
	else if( (buffer[buffer_len - 1] == 0x98) && (data[i] == 0x91) )
	{
		// Log Display can be terminated by another Log Display.  In that
		// case we remain in this handler state.
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}
		buffer_len = 0;
		buffer[buffer_len++] = 0x98;
		buffer[buffer_len++] = data[i];
		header_state = LOG_DISPLAY;        // remain in Log Display
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Log Display ---\n");
		}
	}
	else if( ((buffer[buffer_len - 1] == 0x98)) && (data[i] == 0x3B) )  // 98 and ';'
	{
		// In bursts of back-to-back Display records Log Display can 
		// terminate with a Log Data ";wait" frame
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Display ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// Prepare for log data
		buffer_len = 0;
		buffer[buffer_len++] = 0x98;
		buffer[buffer_len++] = data[i];
		header_state = LOG_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To LOG Data ---\n");
		}	
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}
//--------------------------------------------------------------------
// LOG_Printer    (Passive)
//     This can be terminated by
//     * Log_Display (ordinary status 98 90 54  or  98 90 5C)
//     * Printer data containing status L (printer module requesting next
//       data record from 1022) being passively monitored
//--------------------------------------------------------------------
void LOG_Printer(int i, unsigned char *data)
{
	// This terminates at the beginning of a regular display record
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// Test if Logmode bit in printer status is de-asserted (1 to 0).  
		// For Passive parsing this means return to steady state (ss).
		// buffer[0] is 98, [1] is 90, [2] is status
		if( !(buffer[2] & ST_LOGMODE) )
		{
			// Close the logmode file
			if( f_log != NULL ) {
				fclose( f_log );
				f_log = NULL;
			}

			// Clear the Log mode status bit in the printer status byte
			status_clr_logmode();
			
			// Prepare to return to Steady State
			buffer_len = 0;
			buffer[buffer_len++] = data[i];
			header_state = SS_DISPLAY;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To SS Display ---\n");
			}
		}
		else
		{
			// This is a regular printer module status response in logmode
			// so reset to receive the next
			buffer_len = 0;
			buffer[buffer_len++] = data[i];
			header_state = LOG_DISPLAY;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To LOG Display ---\n");
			}
		}
	}
	else
	{
		// If we are passively monitoring look for 'L' request from
		// printer to 1022 for a log data record
		if( (buffer[buffer_len - 1] == 0x4C) && (data[i] == 0x0d) ) {
			// Push to buffer and display Printer sequence
			buffer[buffer_len++] = data[i];
			
			if( *p_options & DEBUG_DUMP ) {
				printf("--- Log Printer ---\n");
				DumpHexStdout( (const void*)buffer, buffer_len );
			}
			// Now reset to prepare for data record
			buffer_len = 0;
			header_state = LOG_DATA;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To LOG Data ---\n");
			}	
		}
		// Passively monitoring, look for 'R' Report sequence start
		// while in Log mode.  If found, enter Report mode.
		else if( (buffer[buffer_len - 1] == 0x52) && (data[i] == 0x0d) )
		{
			buffer[buffer_len++] = data[i];
			header_state = RPT_START;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To RPT Start ---\n");
			}
		}
		// Passively monitoring, look for 'H' History sequence start
		// while in Log mode.  If found, enter History mode.
		else if( (buffer[buffer_len - 1] == 0x49) && (data[i] == 0x0d) )
		{
			buffer[buffer_len++] = data[i];
			header_state = HST_START;
			if( *p_options & DEBUG_DUMP ) {
				printf("--- To HST Start ---\n");
			}
		}
		else
		{
			buffer[buffer_len] = data[i];
			++buffer_len;
		}
	}
}
//--------------------------------------------------------------------
// LOG_Printer_Active
//     When state machine sends 54 4C 0D request to 1022 for next data record
//     we come to this handler.  It is always terminated by LOG_Data.
//--------------------------------------------------------------------
void LOG_Printer_Active(int i, unsigned char *data)
{
	// Terminate at beginning of LOG_Data
	if( data[i] == 0x98 )
	{
		if( *p_options & DEBUG_DUMP ) {
			printf("--- Log Printer ---\n");
			DumpHexStdout( (const void*)buffer, buffer_len );
		}

		// I don't think we need to test here for log bit dropping in printer
		// status since this is always in response to a 54 4C 0D status L
		// query for data.
		
		// Now reset to move on to Log Data
		buffer_len = 0;
		buffer[buffer_len++] = data[i];
		header_state = LOG_DATA;
		if( *p_options & DEBUG_DUMP ) {
			printf("--- To Log Data ---\n");
		}
	}
	else
	{
		buffer[buffer_len] = data[i];
		++buffer_len;
	}
}


//--------------------------------------------------------------------
// Parse Header
//--------------------------------------------------------------------
void parse_header(int len, unsigned char *data)
{
	double time_diff;

	// Flag for taking a snapshot of refractometer A and B readings
	snapshot_now = time( NULL );
	time_diff = difftime( snapshot_now, snapshot_interval );
	if( (time_diff >= 5) || (time_diff < -1) )
	{
	    snapshot_interval = snapshot_now;
		is_snapshot = 1;
	}

	for( int i = 0; i < len; i++ )
	{
		switch( header_state )
		{
		case SS_UNKNOWN:
			SS_Unknown( i, data );
			break;
		case SS_PAUSE:
			SS_Pause( i, data );
			break;
		case SS_DISPLAY:
			SS_Display( i, data );
			break;
		case SS_PRINTER:
			SS_Printer( i, data );
			break;
		case RPT_START:
			RPT_Start( i, data );
			break;
		case RPT_DATA:
			RPT_Data( i, data );
			break;
		case RPT_DISPLAY:
			RPT_Display( i, data );
			break;
		case RPT_PRINTER:
			RPT_Printer( i, data );
			break;
		case HST_START:
			HST_Start( i, data );
			break;
		case HST_DISPLAY:
			HST_Display( i, data );
			break;
		case HST_PRINTER:
			HST_Printer( i, data );
			break;
		case HST_PRINTER_ACTIVE:
			HST_Printer_Active( i, data );
			break;
		case HST_DATA:
			HST_Data( i, data );
			break;
		case LOG_START:
			LOG_Start( i, data );
			break;
		case LOG_DATA:
			LOG_Data( i, data );
			break;
		case LOG_DISPLAY:
			LOG_Display( i, data );
			break;
		case LOG_PRINTER:
			LOG_Printer( i, data );
			break;
		case LOG_PRINTER_ACTIVE:
			LOG_Printer_Active( i, data );
			break;
			
		default:
		case LAST_STATE:
			break;
		}
	}
}
