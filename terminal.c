/*** include ***/

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <stdbool.h>

/*** macros ***/

#define CTRL_KEY(k) ((k) & 0x1f)

/*** structs ***/

struct editorConfig {
	int screenrows;
	int screencols;
	struct termios original_termios;
};

/*** globals ***/

struct editorConfig editor;

/*** function signatures ***/
void editorRefreshScreen (void);
char editorReadKey (void);

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
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

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
	if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.original_termios) == -1)
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
	if(tcgetattr(STDIN_FILENO, &editor.original_termios) == -1) 
	{
		die("tcgetattr");
	}

	// Call disableRawMode
	atexit(disableRawMode);

	struct termios raw = editor.original_termios;

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
 * 	getWindowSize
 * 
 * 	@param rows
 * 	@param cols
 *  
 *	Return winsize dimensions
 */
int getWindowSize(int * rows, int * cols)
{
	struct winsize ws;

	if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		// then try large values to position cursor at bounded bottom right to return dimensions
		if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
		{
			return -1;
		}
		editorReadKey();
		return -1;
	}
	else
	{
		*cols = ws.ws_col;
		*rows = ws.ws_row;

		return 0;
	}
}

/** 
 * 	initEditor
 *  
 *	Init winsize dimensions
 */
void initEditor(void)
{
	if (getWindowSize(&editor.screenrows, &editor.screencols) == -1)
	{
		die("getWindowSize");
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
 * 	editorDrawRows
 * 
 * 	@param none
 * 
 * 	draw a column of tildes on left side
 * 
 */
void editorDrawRows (void)
{
	int y;

	for (y = 0; y < editor.screenrows; y++)
	{
		write(STDOUT_FILENO, "~\r\n", 3);
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

	editorDrawRows();

	write(STDOUT_FILENO, "\x1b[H", 3);
}

/** 
 * 	getCursorPosition
 * 
 * 	@param none
 * 
 */
void getCursorPosition (void)
{
	if(write(STDOUT_FILENO, "\x1b[6n",4) != 4) 
	{
		return -1;
	}

	printf("\r\n");
	char c;

	while(read(STDIN_FILENO, &c, 1) == 1)
	{
		if(iscntrl(c))
		{
			printf("%d\r\n", c);
		}
		else
		{
			printf("%d ('%c')\r\n", c, c);
		}
	}
	editorReadKey();
	return -1;
}

/*** main ***/

int main() 
{	
	enableRawMode();
	initEditor();

	// infinite loop to read 1 from standard input
	while (true)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}