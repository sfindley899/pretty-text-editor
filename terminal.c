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

/*** function signatures ***/
void editorRefreshScreen (void);

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
	editorRefreshScreen();
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

/** 
 * 	editorReadKey
 * 
 * 	@param none
 * 
 * 	Wait for single byte then return
 * 	@todo handle multiple bytes
 *  
 */
char editorReadKey(void)
{
	int nread;
	char ch;

	while((nread = read(STDERR_FILENO, &ch, 1)) != 1)
	{
		if(nread == -1 && errno != EAGAIN)
		{
			die("read");
		}
	}
	return ch;
}

/** 
 * 	editorProcessKeypress
 * 
 * 	@param none
 * 
 *  handles key press from editorReadKey()
 */
void editorProcessKeypress(void)
{
	char ch = editorReadKey();

	switch(ch)
	{
		case CTRL_KEY('q'):
		{
			exit(0);
			break;
		}
		default:
		{
			break;
		}
	}
}




/** 
 * 	editorRefreshScreen
 * 
 * 	@param none
 * 
 *  clear screen using Erase In Display
 */
void editorRefreshScreen (void)
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
}

/*** main ***/

int main() 
{	
	enableRawMode();
 
	// infinite loop to read 1 from standard input
	while (true)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}