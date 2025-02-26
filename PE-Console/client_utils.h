//--------------------------------------------------------------------
//  client_utils.h
//--------------------------------------------------------------------
#include <unistd.h>
#include <stdlib.h>     // atoi
#include <sys/select.h>
#include <termios.h>    // Contains POSIX terminal control definitions
#include <string.h>     // strcpy

void set_conio_terminal_mode(void);
int kbhit(void);
int getch();
