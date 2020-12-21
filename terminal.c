#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <stdio.h>

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

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON );
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);

	tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main() 
{	
	char c;

	enableRawMode();

	// loop to read 1 from standard input
	while (read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
	{
		// echos control character value otherwise ASCII and character value
		if(iscntrl(c))
		{
			printf("%d\r\n",c);
		}
		else
		{
			printf("%d ('%c')\r\n", c, c);
		}
	}

	return 0;
}