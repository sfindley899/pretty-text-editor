/*** include ***/

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdbool.h>

/*** macros ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"

/*** structs ***/

struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
	struct termios original_termios;
};

struct abuf
{
	char *b;
	int len;
};

/*** globals ***/

struct editorConfig editor;

/*** function signatures ***/
void editorRefreshScreen (void);
char editorReadKey (void);
int getCursorPosition (int *rows, int *cols);
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void editorMoveCursor(char key);

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
		return getCursorPosition(rows, cols);
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
	editor.cx = 0;
	editor.cy = 0;

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
 * 	editorMoveCursor
 * 
 * 	@param none
 * 
 *  handles wasd key press
 */
void editorMoveCursor(char key)
{
	switch (key)
	{
		case 'a':
		{
			editor.cx--;
			break;
		}
		case 'd':
		{
			editor.cx++;
			break;
		}
		case 's':
		{
			editor.cy++;
			break;
		}
		case 'w':
		{
			editor.cy--;
			break;
		}
	}
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
 * 	@param ab buffer
 * 
 * Draw tildes on screen
 */
void editorDrawRows(struct abuf  *ab)
{
	int y;
	int padding;
	char welcome [80];
	int welcomelen;

	for (y = 0; y < editor.screenrows; y++)
	{
		if (y==editor.screenrows / 3)
		{
			welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);

			if(welcomelen > editor.screencols)
			{
				welcomelen = editor.screencols;
			}
			padding = (editor.screencols - welcomelen) / 2;
			if (padding)
			{
				abAppend(ab, "~", 1);
				padding--;
			}
			abAppend(ab, welcome, welcomelen);
			while(padding-- != 0)
			{
				abAppend(ab, " ", 1);
			}
		}
		else
		{
			abAppend(ab, "~", 1);
		}
		
		abAppend(ab, "\x1b[k", 3);

		if(y < editor.screenrows - 1)
		{
			abAppend(ab, "\r\n", 2);
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
	struct abuf ab = ABUF_INIT;

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);

	char buf[32];
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", editor.cy + 1, editor.cx + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/** 
 * 	abAppend
 * 
 * 	@param ab buffer
 *  @param s string
 *  @param len length of string
 * 
 *  append string to buffer
 */
void abAppend(struct abuf *ab, const char *s, int len)
{
	char * new = realloc(ab->b, ab->len + len);

	if (new == NULL)
	{
		return;
	}
	else
	{
		memcpy(&new[ab->len], s, len);
		ab->b = new;
		ab->len += len;
	}
}

/** 
 * 	abFree
 * 
 * 	@param ab buffer
 * 
 *  free buffer string
 */
void abFree(struct abuf *ab)
{
	free(ab->b);
}

/** 
 * 	getCursorPosition
 * 
 * 	@param none
 * 
 */
int getCursorPosition (int * rows, int * cols)
{
	char buf[32];
	unsigned int i = 0;

	if(write(STDOUT_FILENO, "\x1b[6n",4) != 4) 
	{
		return -1;
	}

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
		{
			break;
		}
		if(buf[i] == 'R')
		{
			break;
		}
		i++;
	}

	buf[i] = '\0';

	if (buf[0] != '\x1b' || buf[1] != '[')
	{
		return -1;
	}
	if(sscanf(&buf[2], "%d;%d", rows, cols) != 2)
	{
		return -1;
	}

	return 0;
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