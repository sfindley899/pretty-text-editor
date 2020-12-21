/*** include ***/

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>

/*** macros ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** globals ***/

struct termios original_termios;

/*** functions ***/

/** 
 * 	die
 * 
 * 	@param msg
 *  
 *	Print error message and kill program
 */
void die(const char *msg)
{
	perror(msg);
	exit(1);
}

/** 
 * 	disableRawMode
 * 
 * 	@param none
 *  
 *	Restores original terminal attributes
 */
void disableRawMode(void)
{
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios) == -1)
	{
		die("tcsetattr");
	}
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
	if(tcgetattr(STDIN_FILENO, &original_termios) == -1) 
	{
		die("tcgetattr");
	}

	// Call disableRawMode
	atexit(disableRawMode);

	struct termios raw = original_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON );
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;

	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
	{
		die("tcsetattr");
	}
}

/*** main ***/

int main() 
{	
	enableRawMode();
	char c;
 
	// infinite loop to read 1 from standard input
	while (true)
	{
		c = '\0';

		if(read(STDIN_FILENO, &c, 1) == -1)
		{
			die("read");
		}

		// echos control character value otherwise ASCII and character value
		if(iscntrl(c))
		{
			printf("%d\r\n",c);
		}
		else
		{
			printf("%d ('%c')\r\n", c, c);
		}
		if (c == CTRL_KEY('q')) break;
	}

	return 0;
}