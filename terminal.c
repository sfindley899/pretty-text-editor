/*** include ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdbool.h>

/*** macros ***/

#define CTRL_KEY(k) ((k) & 0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"


/*** enums ***/
enum editorKey {
	ARROW_LEFT = 1000,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN
};

/*** structs ***/
struct abuf
{
	char *b;
	int len;
};

typedef struct erow 
{
	int size;
	char * chars;
} erow;

struct editorConfig {
	int cx, cy;
	int screenrows;
	int screencols;
	int numrows;
	erow *row;
	struct termios original_termios;
};

/*** globals ***/


struct editorConfig editor;

/*** function signatures ***/
void editorRefreshScreen (void);
int editorReadKey (void);
int getCursorPosition (int *rows, int *cols);
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void editorMoveCursor(int key);
void editorOpen(char * filename);
void editorAppendRow(char * string, size_t len);

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
	editor.numrows = 0;
	editor.row = NULL;

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
int editorReadKey(void)
{
	int nread;
	char ch;
	char sequence[3];

	while((nread = read(STDERR_FILENO, &ch, 1)) != 1)
	{
		if(nread == -1 && errno != EAGAIN)
		{
			die("read");
		}
	}

	// handle 4 directional keypress
	if (ch == '\x1b')
	{
		if (read(STDIN_FILENO, &sequence[0], 1) != 1)
		{
			return '\x1b';
		}
		if (read(STDIN_FILENO, &sequence[1], 1) != 1)
		{
			return '\x1b';
		}

		if(sequence[0] == '[')
		{

			if(sequence[1] >= '0' && sequence[1] <= '9')
			{
				if (read(STDERR_FILENO, &sequence[2], 1) != 1)
				{
					return '\x1b';
				}
				if(sequence[2] == '~')
				{
					switch (sequence[1])
					{					
						case '1': 
						{
							return HOME_KEY;
							break;
						}
						case '3': 
						{
							return DEL_KEY;
							break;
						}
						case '4':
						{
							return END_KEY;
							break;
						}
						case '5': 
						{
							return PAGE_UP;
							break;
						}
						case '6':
						{
							return PAGE_DOWN;
							break;
						}
						case '7': 
						{
							return HOME_KEY;
							break;
						}
						case '8':
						{
							return END_KEY;
							break;
						}
					}
				}
			}
			else
			{
				switch (sequence[1])
				{
					case 'A':
					{
						return ARROW_UP;
					}
					case 'B':
					{
						return ARROW_DOWN;
					}
					case 'C':
					{
						return ARROW_RIGHT;
					}
					case 'D':
					{
						return ARROW_LEFT;
					}
					case 'H':
					{
						return HOME_KEY;
					}
					case 'F':
					{
						return END_KEY;
					}
				}
			}
		}
		else if (sequence[0] == 'O')
		{
			switch (sequence[1])
			{
				case 'H':				
				{
					return HOME_KEY;
				}
				case 'F':				
				{
					return END_KEY;
				}
			}
		}
		
		return '\x1b';
	}
	else
	{
		return ch;
	}
}

/** 
 * 	editorOpen
 * 
 * 	@param none
 * 
 *  handles file i/o
 *  @todo init editor to handle dynamically allocated data 
 */
void editorOpen(char * filename)
{
	char * line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	FILE *fp = fopen(filename, "r");
	if(!fp) 
	{
		die("fopen");
	}

	linelen = getline(&line,&linecap,fp);
	if (linelen != -1)
	{
		while(linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
		{
			linelen--;
		}
		editor.row->size = linelen;
		editor.row->chars = malloc(linelen + 1);
		memcpy(editor.row->chars, line, linelen);
		editor.row->chars[linelen] = '\0';
		editor.numrows = 1;

		editorAppendRow(line, linelen);
	}
	free(line);
	fclose(fp);
}

/** 
 * 	editorMoveCursor
 * 
 * 	@param none
 * 
 *  handles wasd key press
 */
void editorMoveCursor(int key)
{
	switch (key)
	{
		case ARROW_LEFT:
		{
			if(editor.cx != 0)
			{
				editor.cx--;
			}
			break;
		}
		case ARROW_RIGHT:
		{
			if(editor.cx != editor.screencols - 1)
			{
				editor.cx++;
			}
			break;
		}
		case ARROW_DOWN:
		{
			if(editor.cy != editor.screenrows - 1)
			{
				editor.cy++;
			}
			break;	
		}
		case ARROW_UP:
		{
			if(editor.cy != 0)
			{
				editor.cy--;
			}
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
	int ch = editorReadKey();
	int times;

	switch(ch)
	{
		case CTRL_KEY('q'):
		{      
			write(STDOUT_FILENO, "\x1b[2J", 4);
      		write(STDOUT_FILENO, "\x1b[H", 3);		
			exit(0);
			break;
		}
		case HOME_KEY:
		{
			editor.cx = 0;
			break;
		}
		case END_KEY:
		{
			editor.cx = editor.screencols - 1;
			break;
		}
		case PAGE_UP:
		case PAGE_DOWN:
		{
			times = editor.screenrows;
			while(times-- != 0)
			{
				editorMoveCursor(ch == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;
		}

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
		{
			editorMoveCursor(ch);
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
	int len;
	int padding;
	char welcome [80];
	int welcomelen;

	for (y = 0; y < editor.screenrows; y++)
	{
		if(y >= editor.numrows)
		{
			if (editor.numrows == 0 && y==editor.screenrows / 3)
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
		}
		else
		{
			len = editor.row[y].size;
			if(len > editor.screencols)
			{
				len = editor.screencols;
			}
			abAppend(ab, editor.row[y].chars, len);
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

/** 
 * 	editorAppendRow
 * 
 * 	@param string
 *  @param len
 * 
 */
void editorAppendRow(char * string, size_t len)
{
	int at;

	editor.row = realloc(editor.row, sizeof(erow) * (editor.numrows + 1));
	at = editor.numrows;
	editor.row[at].size = len;
	editor.row[at].chars = malloc(len + 1);
	memcpy(editor.row[at].chars, string, len);
	editor.row[at].chars[len] = '\0';
	editor.numrows++;
}

/*** main ***/

int main(int argc, char *argv[]) 
{	
	enableRawMode();
	initEditor();
	
	if(argc >= 2)
	{
		editorOpen(argv[1]);
	}

	// infinite loop to read 1 from standard input
	while (true)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}