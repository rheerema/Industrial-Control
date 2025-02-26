
//--------------------------------------------------------------------
//  Parser for 1022 RS-485 Protocol
//--------------------------------------------------------------------

void parse_open(unsigned int *options, unsigned int *control, int *port);
int status_is_logmode();
void parse_close();
void parse_header(int len, unsigned char *data);

void SS_Pause_Active(int i, unsigned char *data);

//--------------------------------------------------------------------
// Header State Macine stuff
//--------------------------------------------------------------------
typedef enum {
	// Steady State handlers
	SS_UNKNOWN,   // don't yet know where we are
	SS_PAUSE,     // in between states
	SS_DISPLAY,   // VFD display
	SS_PRINTER,   // Printer module
	// Report handlers
	RPT_START,
	RPT_DATA,
	RPT_DISPLAY,
	RPT_PRINTER,
	// History handlers
	HST_START,
	HST_DISPLAY,
	HST_PRINTER,
	HST_PRINTER_ACTIVE,
	HST_DATA,
	// Logmode handlers
	LOG_START,
	LOG_DATA,
	LOG_DISPLAY,
	LOG_PRINTER,
	LOG_PRINTER_ACTIVE,
	LAST_STATE
} state_t;

// --- Status Bits ---
// "not used" bits are commented out in COMBO.C (1022 code)
typedef enum
{
	ST_NOPAPER     = 1 << 7,    // not used
	ST_PRWON       = 1 << 6,    // actual but disagrees with COMBO.C (which says 0x20)
	ST_PROFFLN     = 1 << 5,    // not used
	ST_LOGMODE     = 1 << 4,
	ST_PRBUSY      = 1 << 3,
	ST_READY       = 1 << 2,
	ST_NOTRDY      = 1 << 1,    // not used
	ST_UNPLUG      = 1 << 0,    // not used
} STATUS_BIT;

// File Size for Report, History, and log file names
// Bear in mind that this has to fit within MSG_MAX_PAYLOAD (message_services.h)
// when a data sequence is complete
#define DATA_FILENAME_SIZE 128

