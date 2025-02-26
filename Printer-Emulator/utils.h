
//--------------------------------------------------------------------
//  utils.h
//--------------------------------------------------------------------

void DumpHex(const void* data, size_t size, FILE *f_out);
int serial_port_open(int *serial_port, char *port_name);
int unique_filename( char *base, char *name_out, int name_sz );
int control_receive_msg( unsigned int *p_control );

// --- Option Bit Fields ---
// Options are set at the beginning of runtime and remain that way
// through the lifetime of the program
typedef enum
{
	DEBUG_DUMP  = 1 << 7, // Debug dump of parser state machine transitions
	UNIT_TEST   = 1 << 6, // Unit Test with file specified by index
	CAPTURE     = 1 << 5, // Capture data on the wire and write to a file
	TARGET      = 1 << 4, // Target hardware is the execution environment
	OPTION3     = 1 << 3, //
	OPTION2     = 1 << 2, //
	LOW_LATENCY = 1 << 1, //  Serial port low latency
	ACTIVE_MODE = 1 << 0  //  Program emulates a Printer Module
} OPTION_BIT;

// --- Control Bit Fields ---
// Control bits are set and reset dynamically as the program runs
typedef enum
{
	MESSAGE_SRC     = 1 << 7,  // Control through message IPC
	CONTROL6        = 1 << 6,
	CONTROL5        = 1 << 5,
	CONTROL4        = 1 << 4,
	LOGMODE_OFF_REQ = 1 << 3,
	LOGMODE_ON_REQ  = 1 << 2,
	HISTORY_REQ     = 1 << 1,
	REPORT_REQ      = 1 << 0,
} CONTROL_BIT;


// Location of readings.txt, report, history, and log files

// On the desktop they all go to the same place
#define DESKTOP_DISK_DIR "./Data"

// On the target there are two locations
// Target log files go to disk
#define TARGET_DISK_DIR "/var/log/lsc"

// Target readings.txt, report.txt, history.txt go to ramdisk
#define TARGET_RAM_DIR "/mnt/ramdisk/lsc"
