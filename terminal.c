#include <unistd.h>
#include <stdlib.h>
#include <termios.h>

struct termios original_termios;


/** 
 * 	disableRawMode
 * 
 * 	@param none
 *  
 *	Restores original terminal attributes
 */
void disableRawMode(void)
{
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

/** 
 * 	enableRawMode
 * 
 * 	@param none
 *  
 *	Sets terminal attributes to disable
 *	echo of keystrokes in terminal
 */
void enableRawMode(void)
{
	// Saves default terminal settings
	tcgetattr(STDIN_FILENO, &original_termios);

	// Call disableRawMode
	atexit(disableRawMode);

	struct termios raw = original_termios;

	tcgetattr(STDIN_FILENO, &raw);

	raw.c_lflag &= ~(ECHO);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() 
{	
	char c;

	enableRawMode();

	// loop to read 1 byte from standard input
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
	{
		/* */
	}

	return 0;
}