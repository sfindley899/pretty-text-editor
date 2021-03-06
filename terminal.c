#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

/***includes ***/ 
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <stdbool.h>

/***macros ***/
#define CTRL_KEY(k)((k) &0x1f)
#define ABUF_INIT {NULL, 0}
#define KILO_VERSION "0.0.1"
#define TAB_STOP 8
#define QUIT_TIMES 3
#define HIGHLIGHT_NUMBERS (1 << 0)
#define HIGHLIGHT_STRINGS (1 << 1)
#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/***enums ***/
enum editorKey
{
	BACKSPACE = 127,
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

enum editorHighlight
{
	HL_NORMAL = 0,
		HL_COMMENT,
		HL_MULTI_COMMENT,
		HL_KEYWORD1,
		HL_KEYWORD2,
		HL_STRING,
		HL_NUMBER,
		HL_MATCH
};

/***structs ***/
struct abuf
{
	char *b;
	int len;
};

typedef struct erow
{
	int idx;
	int size;
	int rsize;
	char *chars;
	char *render;
	unsigned char *hl;
	bool hl_open_comment;
}

erow;

struct editorConfig
{
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int screenrows;
	int screencols;
	int numrows;
	int dirty;
	char statusmsg[80];
	time_t statusmsg_timestamp;
	erow * row;
	char *filename;
	struct editorSyntax * syntax;
	struct termios original_termios;
};

struct editorSyntax
{
	char *filetype;
	char **filematch;
	char **keywords;
	char *singleline_comment_start;
	char *multi_comment_start;
	char *multi_comment_start2;
	char *multi_comment_end;
	int flags;
};

/***globals ***/

struct editorConfig editor;
char *C_HL_extensions[] = { ".c", ".h", ".cpp", NULL
};
char *C_HL_keywords[] = { "switch", "if", "while", "for", "break", "continue", "return", "else",
	"struct", "union", "typedef", "static", "enum", "class", "case",
	"int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
	"void|", NULL
};

struct editorSyntax HLDB[] = {
		{
		"c",
		C_HL_extensions,
		C_HL_keywords,
		"//", "/*", "/**", "*/",
		HIGHLIGHT_NUMBERS | HIGHLIGHT_STRINGS
	},
};

/***function signatures ***/
void editorRefreshScreen(void);
int editorReadKey(void);
int getCursorPosition(int *rows, int *cols);
void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);
void editorMoveCursor(int key);
void editorOpen(char *filename);
void editorInsertRow(int at, char *string, size_t len);
void editorUpdateRow(erow *row);
void editorDrawStatusBar(struct abuf *ab);
void editorSetStatusMessage(const char *fmt, ...);
void editorDrawMessageBar(struct abuf *ab);
void editorInsertNewLine(void);
void editorRowInsertChar(erow *row, int at, int c);
void editorDelChar(void);
void editorInsertChar(int ch);
char *editorRowsToString(int *buflen);
void editorSave(void);
char *editorPrompt(char *prompt, void(*callback)(char *, int));
void editorUpdateSyntax(erow *row);
int editorSyntaxToColor(int hl);
void editorSelectSyntaxHighlight(void);

/***functions ***/

/**
 *	die
 *
 *	@param msg Failure message string
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
 *	disableRawMode
 *
 *	@param none
 *
 *	Restores original terminal attributes
 */
void disableRawMode(void)
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &editor.original_termios) == -1)
	{
		die("tcsetattr");
	}
}

/**
 *	enableRawMode
 *
 *	@param none
 *
 *	Sets terminal attributes
 */
void enableRawMode(void)
{
	// Saves default terminal settings
	if (tcgetattr(STDIN_FILENO, &editor.original_termios) == -1)
	{
		die("tcgetattr");
	}

	// Call disableRawMode
	atexit(disableRawMode);

	struct termios raw = editor.original_termios;

	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
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
 *	getWindowSize
 *
 *	@param rows amount of terminal rows
 *	@param cols amount of terminal columns 
 *
 *	Return winsize dimensions
 */
int getWindowSize(int *rows, int *cols)
{
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
	{
		// then try large values to position cursor at bounded bottom right to return dimensions
		if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
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
 *	initEditor
 *
 *	Init winsize dimensions
 */
void initEditor(void)
{
	editor.cx = 0;
	editor.cy = 0;
	editor.rx = 0;
	editor.coloff = 0;
	editor.rowoff = 0;
	editor.numrows = 0;
	editor.dirty = 0;
	editor.row = NULL;
	editor.filename = NULL;
	editor.statusmsg[0] = '\0';
	editor.statusmsg_timestamp = 0;
	editor.syntax = NULL;

	if (getWindowSize(&editor.screenrows, &editor.screencols) == -1)
	{
		die("getWindowSize");
	}

	editor.screenrows -= 2;
}

/**
 *	editorDrawStatusBar
 *	
 * 	@param ab buffer
 *
 */
void editorDrawStatusBar(struct abuf *ab)
{
	int len = 0, rlen;
	char status[80], rstatus[80];

	abAppend(ab, "\x1b[7m", 4);
	len = snprintf(status, sizeof(status), "%.20s - %d lines %s", \
		(editor.filename != NULL) ? editor.filename : "[Untitled]", editor.numrows, \
		 editor.dirty ? "(modified)" : "");
	rlen = snprintf(rstatus, sizeof(rstatus), " %s | %d/%d", \
		editor.syntax ? editor.syntax->filetype : "no filetype", editor.cy + 1, editor.numrows);
	if (len > editor.screencols)
	{
		len = editor.screencols;
	}

	abAppend(ab, status, len);

	while (len < editor.screencols)
	{
		if (editor.screencols - len == rlen)
		{
			abAppend(ab, rstatus, rlen);
			break;
		}
		else
		{
			abAppend(ab, " ", 1);
			len++;
		}
	}

	abAppend(ab, "\x1b[m", 3);
	abAppend(ab, "\r\n", 3);
}

/**
 *	editorDrawMessageBar
 *	
 * 	@param ab buffer
 *
 */
void editorDrawMessageBar(struct abuf *ab)
{
	int msglen = strlen(editor.statusmsg);
	abAppend(ab, "\x1b[K", 3);

	if (msglen > editor.screencols)
	{
		msglen = editor.screencols;
	}

	if ((msglen > 0) && (time(NULL) - editor.statusmsg_timestamp < 5))
	{
		abAppend(ab, editor.statusmsg, msglen);
	}
}

/**
 *	editorSetStatusMessage
 *	
 * 	@param fmt format parameters
 *
 */
void editorSetStatusMessage(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(editor.statusmsg, sizeof(editor.statusmsg), fmt, ap);
	va_end(ap);
	editor.statusmsg_timestamp = time(NULL);
}

/**
 *	editorRowCxToRx
 *
 *	@param row editor row
 *	@param cx column position
 *
 */
int editorRowCxToRx(erow *row, int cx)
{
	int rx = 0, j;

	for (j = 0; j < cx; j++)
	{
		if (row->chars[j] == '\t')
		{
			rx += (TAB_STOP - 1) - (rx % TAB_STOP);
		}

		rx++;
	}

	return rx;
}

/**
 *	editorRowRxtoCx
 *
 *	@param row editor row
 *	@param rx render position
 *
 */
int editorRowRxToCx(erow *row, int rx)
{
	int cur_rx = 0;
	int cx;

	for (cx = 0; cx < row->size; cx++)
	{
		if (row->chars[cx] == '\t')
		{
			cur_rx += (TAB_STOP - 1) - (cur_rx % TAB_STOP);
		}

		cur_rx++;

		if (cur_rx > rx)
		{
			return cx;
		}
	}

	return cx;
}

/**
 *	editorReadKey
 *
 *	@param none
 *
 *	Wait for single byte then return
 *
 */
int editorReadKey(void)
{
	int nread;
	char ch;
	char sequence[3];

	while ((nread = read(STDERR_FILENO, &ch, 1)) != 1)
	{
		if (nread == -1 && errno != EAGAIN)
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

		if (sequence[0] == '[')
		{
			if (sequence[1] >= '0' && sequence[1] <= '9')
			{
				if (read(STDERR_FILENO, &sequence[2], 1) != 1)
				{
					return '\x1b';
				}

				if (sequence[2] == '~')
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
 *	editorOpen
 *
 *	@param filename path to file
 * 	
 *	handles file i/o
 */
void editorOpen(char *filename)
{
	if (editor.filename != NULL)
	{
		free(editor.filename);
	}

	editor.filename = strdup(filename);
	editorSelectSyntaxHighlight();

	FILE *fp = fopen(filename, "r");
	char *line = NULL;
	size_t linecap = 0;
	ssize_t linelen;

	if (!fp)
	{
		die("fopen");
	}

	linelen = getline(&line, &linecap, fp);
	while ((linelen = getline(&line, &linecap, fp)) != -1)
	{
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r'))
		{
			linelen--;
		}

		editorInsertRow(editor.numrows, line, linelen);
	}

	free(line);
	fclose(fp);
	editor.dirty = 0;
}

/**
 *	editorSave
 *
 *	@param none
 *	
 */
void editorSave(void)
{
	int len, fd;
	char *buf;

	if (editor.filename == NULL)
	{
		editor.filename = editorPrompt("Save as: %s (ESC to cancel)", NULL);
		if (editor.filename == NULL)
		{
			editorSetStatusMessage("Save aborted");
			return;
		}

		editorSelectSyntaxHighlight();
	}

	buf = editorRowsToString(&len);
	fd = open(editor.filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1)
	{
		if (ftruncate(fd, len) != -1)
		{
			if (write(fd, buf, len) == len)
			{
				close(fd);
				free(buf);
				editor.dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}

		close(fd);
	}

	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));

}

/**
 *	editorFindCallback
 * 
 * 	@param query search query
 * 	@param key character
 *
 */
void editorFindCallback(char *query, int key)
{
	static int last_match = -1;
	static int direction = 1;
	static int saved_hl_line;
	static char *saved_hl = NULL;

	int i, current;
	char *match;
	erow * row;

	if (saved_hl)
	{
		memcpy(editor.row[saved_hl_line].hl, saved_hl, editor.row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b')
	{
		last_match = -1;
		direction = 1;
		return;
	}
	else if (key == ARROW_DOWN || ARROW_RIGHT)
	{
		direction = 1;
	}
	else if (key == ARROW_UP || ARROW_LEFT)
	{
		direction = -1;
	}
	else
	{
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1)
	{
		direction = 1;
	}

	current = last_match;
	for (i = 0; i < editor.numrows; i++)
	{
		current += direction;
		if (current == -1)
		{
			current = editor.numrows - 1;
		}
		else if (current == editor.numrows)
		{
			current = 0;
		}

		row = &editor.row[current];
		match = strstr(row->render, query);

		if (match)
		{
			last_match = current;
			editor.cy = current;
			editor.cx = editorRowRxToCx(row, match - row->render);
			editor.rowoff = editor.numrows;

			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

/**
 *	editorFind
 * 
 * 	@param none
 *
 */
void editorFind(void)
{
	char *query = NULL;
	int saved_cx = editor.cx;
	int saved_cy = editor.cy;
	int saved_rowoff = editor.rowoff;
	int saved_coloff = editor.coloff;

	query = editorPrompt("Search: %s (ESC/Arrows/Enter)", editorFindCallback);

	if (query != NULL)
	{
		free(query);
	}
	else
	{
		editor.cx = saved_cx;
		editor.cy = saved_cy;
		editor.rowoff = saved_rowoff;
		editor.coloff = saved_coloff;
	}
}

/**
 *	editorMoveCursor
 *
 *	@param key keypad character
 *
 *	handles wasd key press
 */
char *editorPrompt(char *prompt, void(*callback)(char *, int))
{
	size_t bufsize = 128, buflen = 0;
	char *buf = malloc(bufsize);
	int ch;

	buf[0] = '\0';
	while (1)
	{
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		ch = editorReadKey();
		if (ch == DEL_KEY || ch == CTRL_KEY('h') || ch == BACKSPACE)
		{
			if (buflen != 0)
			{
				buf[--buflen] = '\0';
			}
		}
		else if (ch == '\x1b')
		{
			editorSetStatusMessage("");
			if (callback)
			{
				callback(buf, ch);
			}

			free(buf);
			return NULL;
		}
		else if (ch == '\r')
		{
			if (buflen != 0)
			{
				editorSetStatusMessage("");
				if (callback)
				{
					callback(buf, ch);
				}

				return buf;
			}
		}
		else if (!iscntrl(ch) && ch < 128)
		{
			if (buflen == bufsize - 1)
			{
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}

			buf[buflen++] = ch;
			buf[buflen] = '\0';
		}

		if (callback)
		{
			callback(buf, ch);
		}
	}
}

/**
 *	editorMoveCursor
 *
 *	@param key keypad character
 *
 *	handles wasd key press
 */
void editorMoveCursor(int key)
{
	erow * row;
	int rowlen;

	if (editor.cy >= editor.numrows)
	{
		row = NULL;
	}
	else
	{
		row = &editor.row[editor.cy];
	}

	switch (key)
	{
		case ARROW_LEFT:
			{
				if (editor.cx != 0)
				{
					editor.cx--;
				}
				else if (editor.cy > 0)
				{
					editor.cy--;
					editor.cx = editor.row[editor.cy].size;
				}

				break;
			}

		case ARROW_RIGHT:
			{
				if (row != NULL && editor.cx < row->size)
				{
					editor.cx++;
				}
				else if (row != NULL && editor.cx == row->size)
				{
					editor.cy++;
					editor.cx = 0;
				}

				break;
			}

		case ARROW_DOWN:
			{
				if (editor.cy < editor.numrows)
				{
					editor.cy++;
				}

				break;
			}

		case ARROW_UP:
			{
				if (editor.cy != 0)
				{
					editor.cy--;
				}

				break;
			}
	}

	if (editor.cy >= editor.numrows)
	{
		row = NULL;
	}
	else
	{
		row = &editor.row[editor.cy];
	}

	if (row != NULL)
	{
		rowlen = row->size;
	}
	else
	{
		rowlen = 0;
	}

	if (editor.cx > rowlen)
	{
		editor.cx = rowlen;
	}
}

/**
 *	editorProcessKeypress
 *
 *	@param none
 * 
 *	handles key press from editorReadKey()
 */
void editorProcessKeypress(void)
{
	static int quit_times = QUIT_TIMES;
	int ch = editorReadKey();
	int times;

	switch (ch)
	{
		case '\r':
			{
				editorInsertNewLine();
				break;
			}

		case CTRL_KEY('q'):
			{
				if (editor.dirty && quit_times > 0)
				{
					editorSetStatusMessage("WARNING!!! File has unsaved changes. "\
						"Press Ctrl-Q %d more times to quit.", quit_times);
					quit_times--;
					return;
				}

				write(STDOUT_FILENO, "\x1b[2J", 4);
				write(STDOUT_FILENO, "\x1b[H", 3);
				exit(0);
				break;
			}

		case CTRL_KEY('s'):
			{
				editorSave();
				break;
			}

		case HOME_KEY:
			{
				editor.cx = 0;
				break;
			}

		case END_KEY:
			{
				if (editor.cy < editor.numrows)
				{
					editor.cx = editor.row[editor.cy].size;
				}

				break;
			}

		case CTRL_KEY('f'):
			{
				editorFind();
				break;
			}

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			{
				if (ch == DEL_KEY)
				{
					editorMoveCursor(ARROW_RIGHT);
				}

				editorDelChar();
				break;
			}

		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (ch == PAGE_UP)
				{
					editor.cy = editor.rowoff;
				}
				else if (ch == PAGE_DOWN)
				{
					editor.cy = editor.rowoff + editor.screenrows - 1;
					if (editor.cy > editor.numrows)
					{
						editor.cy = editor.numrows;
					}
				}

				times = editor.screenrows;
				while (times-- != 0)
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

		case CTRL_KEY('l'):
		case '\x1b':
			{
				break;
			}

		default:
			{
				editorInsertChar(ch);
				break;
			}
	}

	quit_times = QUIT_TIMES;
}

/**
 *	editorDrawRows
 *
 *	@param ab buffer
 *
 */
void editorDrawRows(struct abuf *ab)
{
	int y, len, j, clen, color, current_color = -1;
	char *ch;
	unsigned char *hl;
	int filerow;
	char buf[16], symbol;

	for (y = 0; y < editor.screenrows; y++)
	{
		filerow = y + editor.rowoff;
		if (filerow >= editor.numrows)
		{
			if (editor.numrows == 0 && y == editor.screenrows / 3)
			{
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome),
					"Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > editor.screencols) welcomelen = editor.screencols;
				int padding = (editor.screencols - welcomelen) / 2;
				if (padding)
				{
					abAppend(ab, "~", 1);
					padding--;
				}

				while (padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			}
			else
			{
				abAppend(ab, "~", 1);
			}
		}
		else
		{
			len = editor.row[filerow].rsize - editor.coloff;
			if (len < 0)
			{
				len = 0;
			}

			if (len > editor.screencols)
			{
				len = editor.screencols;
			}

			ch = &editor.row[filerow].render[editor.coloff];
			hl = &editor.row[filerow].hl[editor.coloff];
			current_color = -1;
			for (j = 0; j < len; j++)
			{
				if (iscntrl(ch[j]))
				{
					if (ch[j] <= 26)
					{
						symbol = '@' + ch[j];
					}
					else
					{
						symbol = '?';
					}

					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &symbol, 1);
					abAppend(ab, "\x1b[m", 3);

					if (current_color != -1)
					{
						clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}
				}
				else if (hl[j] == HL_NORMAL)
				{
					if (current_color != -1)
					{
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}

					abAppend(ab, &ch[j], 1);
				}
				else
				{
					color = editorSyntaxToColor(hl[j]);
					if (color != current_color)
					{
						current_color = color;
						clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
					}

					abAppend(ab, &ch[j], 1);
				}
			}

			abAppend(ab, "\x1b[39m", 5);
		}

		abAppend(ab, "\x1b[K", 3);
		abAppend(ab, "\r\n", 2);
	}
}

/**
 *	editorScroll
 *
 *	@param none
 *
 */
void editorScroll(void)
{
	editor.rx = 0;
	if (editor.cy < editor.numrows)
	{
		editor.rx = editorRowCxToRx(&editor.row[editor.cy], editor.cx);
	}

	if (editor.cy < editor.rowoff)
	{
		editor.rowoff = editor.cy;
	}

	if (editor.cy >= editor.rowoff + editor.screenrows)
	{
		editor.rowoff = editor.cy - editor.screenrows + 1;
	}

	if (editor.rx < editor.coloff)
	{
		editor.coloff = editor.rx;
	}

	if (editor.rx >= editor.coloff + editor.screencols)
	{
		editor.coloff = editor.rx - editor.screencols + 1;
	}
}

/**
 *	editorRefreshScreen
 *
 *	@param none
 *
 *	clear screen using Erase In Display
 */
void editorRefreshScreen(void)
{
	struct abuf ab = ABUF_INIT;
	char buf[32];

	editorScroll();

	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);

	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (editor.cy - editor.rowoff) + 1, (editor.rx - editor.coloff) + 1);
	abAppend(&ab, buf, strlen(buf));

	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

/**
 *	abAppend
 *
 *	@param ab buffer
 *	@param s string
 *	@param len length of string
 * 
 *	@todo append string to buffer
 */
void abAppend(struct abuf *ab, const char *s, int len)
{
	char *new = realloc(ab->b, ab->len + len);

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
 *	abFree
 *
 *	@param ab buffer
 *
 *	@todo free buffer string
 */
void abFree(struct abuf *ab)
{
	free(ab->b);
}

/**
 *	getCursorPosition
 *
 *	@param none
 *
 */
int getCursorPosition(int *rows, int *cols)
{
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
	{
		return -1;
	}

	while (i < sizeof(buf) - 1)
	{
		if (read(STDIN_FILENO, &buf[i], 1) != 1)
		{
			break;
		}

		if (buf[i] == 'R')
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

	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
	{
		return -1;
	}

	return 0;
}

/**
 *	is_separator
 *
 *	@param ch character
 *
 */
bool is_separator(int ch)
{
	return isspace(ch) || ch == '\0' || strchr(",.()+-/*=~%<>[];", ch) != NULL;
}

/**
 *	editorUpdateSyntax
 *
 *	@param row editor row
 *
 *
 */
void editorUpdateSyntax(erow *row)
{
	int i = 0, j = 0;
	bool prev_separator = true, in_comment = false, changed;
	int in_string = 0;
	int scs_len = 0, mce_len = 0, mcs_len = 0, mcs2_len = 0;
	bool keyword2;
	int keyword_len;
	unsigned char prev_hl;
	char ch;
	char *scs, *mcs, *mcs2, *mce;
	char **keywords;

	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);

	if (editor.syntax == NULL)
	{
		return;
	}
	else
	{
		keywords = editor.syntax->keywords;
		scs = editor.syntax->singleline_comment_start;
		mcs = editor.syntax->multi_comment_start;
		mcs2 = editor.syntax->multi_comment_start2;
		mce = editor.syntax->multi_comment_end;

		if (scs != NULL)
		{
			scs_len = strlen(scs);
		}

		if (mcs != NULL)
		{
			mcs_len = strlen(mcs);
		}

		if (mcs2 != NULL)
		{
			mcs2_len = strlen(mcs2);
		}

		if (mce != NULL)
		{
			mce_len = strlen(mce);
		}

		in_comment = (row->idx > 0 && editor.row[row->idx - 1].hl_open_comment);

		while (i < row->rsize)
		{
			ch = row->render[i];

			if (i > 0)
			{
				prev_hl = row->hl[i - 1];
			}
			else
			{
				prev_hl = HL_NORMAL;
			}

			if ((scs_len != 0) && !in_string && !in_comment)
			{
				if (!strncmp(&row->render[i], scs, scs_len))
				{
					memset(&row->hl[i], HL_COMMENT, row->rsize - i);
					break;
				}
			}

			if ((mcs2_len || mcs_len) && mce_len && (in_string == false))
			{
				if (in_comment != false)
				{
					row->hl[i] = HL_MULTI_COMMENT;
					if (strncmp(&row->render[i], mce, mce_len) == 0)
					{
						memset(&row->hl[i], HL_MULTI_COMMENT, mce_len);
						i += mce_len;
						in_comment = false;
						prev_separator = true;
						continue;
					}
					else
					{
						i++;
						continue;
					}
				}
				else if (strncmp(&row->render[i], mcs, mcs_len) == 0)
				{
					memset(&row->hl[i], HL_MULTI_COMMENT, mcs_len);
					i += mcs_len;
					in_comment = true;
					continue;
				}
				else if (strncmp(&row->render[i], mcs2, mcs2_len) == 0)
				{
					memset(&row->hl[i], HL_MULTI_COMMENT, mcs2_len);
					i += mcs2_len;
					in_comment = true;
					continue;
				}
			}

			if (editor.syntax->flags &HIGHLIGHT_STRINGS)
			{
				if (in_string != 0)
				{
					row->hl[i] = HL_STRING;
					if (ch == '\\' && i + 1 < row->rsize)
					{
						row->hl[i + 1] = HL_STRING;
						i += 2;
						continue;
					}

					if (ch == in_string)
					{
						in_string = 0;
					}

					i++;
					prev_separator = true;
					continue;
				}
				else
				{
					if (ch == '"' || ch == '\'')
					{
						in_string = ch;
						row->hl[i] = HL_STRING;
						i++;
						continue;
					}
				}
			}

			if (editor.syntax->flags &HIGHLIGHT_NUMBERS)
			{
				if ((isdigit(ch) && (prev_separator || prev_hl == HL_NUMBER)) ||
					(ch == '.' && prev_hl == HL_NUMBER))
				{
					row->hl[i] = HL_NUMBER;
					i++;
					prev_separator = false;
					continue;
				}
			}

			if (prev_separator != 0)
			{
				for (j = 0; keywords[j]; j++)
				{
					keyword_len = strlen(keywords[j]);
					keyword2 = keywords[j][keyword_len - 1] == '|';
					if (keyword2 != false)
					{
						keyword_len--;
					}

					if (!strncmp(&row->render[i], keywords[j], keyword_len) &&
						is_separator(row->render[i + keyword_len]))
					{
						memset(&row->hl[i], (keyword2 != false) ? HL_KEYWORD2 : HL_KEYWORD1, keyword_len);
						i += keyword_len;
						break;
					}
				}

				if (keywords[j] != NULL)
				{
					prev_separator = false;
					continue;
				}
			}

			prev_separator = is_separator(ch);
			i++;
		}

		changed = (row->hl_open_comment != in_comment);
		row->hl_open_comment = in_comment;
		if (changed && (row->idx + 1 < editor.numrows))
		{
			editorUpdateSyntax(&editor.row[row->idx + 1]);
		}
	}
}

/**
 *	editorInsertRow
 *
 *	@param at editor row character position
 *	@param string
 *	@param len string length 
 *
 */
void editorInsertRow(int at, char *string, size_t len)
{
	if (at < 0 || at > editor.numrows)
	{
		return;
	}
	else
	{
		editor.row = realloc(editor.row, sizeof(erow) *(editor.numrows + 1));
		memmove(&editor.row[at + 1], &editor.row[at], sizeof(erow) *(editor.numrows - at));

		for (int j = at + 1; j <= editor.numrows; j++)
		{
			editor.row[j].idx++;
		}

		editor.row[at].idx = at;
		editor.row[at].size = len;
		editor.row[at].chars = malloc(len + 1);
		memcpy(editor.row[at].chars, string, len);
		editor.row[at].chars[len] = '\0';

		editor.row[at].rsize = 0;
		editor.row[at].render = NULL;
		editor.row[at].hl = NULL;
		editor.row[at].hl_open_comment = false;
		editorUpdateRow(&editor.row[at]);

		editor.numrows++;
		editor.dirty++;
	}
}

/**
 *	editorRowAppendString
 *
 *	@param row editor row
 *	@param string string 
 * 	@param len string length
 *
 */
void editorRowAppendString(erow *row, char *string, size_t len)
{
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], string, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	editor.dirty++;
}

/**
 *	editorFreeRow
 *
 *	@param row editor row
 *
 */
void editorFreeRow(erow *row)
{
	free(row->render);
	free(row->chars);
	free(row->hl);
}

/**
 *	editorDelRow
 *
 *	@param at editor row character position
 *
 */
void editorDelRow(int at)
{
	if (at < 0 || at >= editor.numrows)
	{
		return;
	}
	else
	{
		editorFreeRow(&editor.row[at]);
		memmove(&editor.row[at], &editor.row[at + 1], sizeof(erow) *(editor.numrows - at - 1));
		for (int j = at; j < editor.numrows - 1; j++)
		{
			editor.row[j].idx--;
		}

		editor.numrows--;
		editor.dirty++;
	}
}

/**
 *	editorInsertNewLine
 *
 *	@param none
 *
 */
void editorInsertNewLine(void)
{
	erow * row;
	if (editor.cx == 0)
	{
		editorInsertRow(editor.cy, "", 0);
	}
	else
	{
		row = &editor.row[editor.cy];
		editorInsertRow(editor.cy + 1, &row->chars[editor.cx], row->size - editor.cx);
		row = &editor.row[editor.cy];
		row->size = editor.cx;
		row->chars[row->size] = '\0';
		editorUpdateRow(row);
	}

	editor.cy++;
	editor.cx = 0;
}

/**
 *	editorRowInsertChar
 *
 *	@param row editor row
 *	@param at char position
 *	@param ch character
 *
 */
void editorRowInsertChar(erow *row, int at, int ch)
{
	if (at < 0 || at > row->size)
	{
		at = row->size;
	}

	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = ch;
	editorUpdateRow(row);
	editor.dirty++;
}

/**
 *	editorInsertChar
 * 
 *	@param ch character
 *
 */
void editorInsertChar(int ch)
{
	if (editor.cy == editor.numrows)
	{
		editorInsertRow(editor.numrows, "", 0);
	}

	editorRowInsertChar(&editor.row[editor.cy], editor.cx, ch);
	editor.cx++;
}

/**
 *	editorRowDelChar
 * 
 *	@param row editor row
 *	@param at char position
 *
 */
void editorRowDelChar(erow *row, int at)
{
	if (at < 0 || at >= row->size)
	{
		return;
	}
	else
	{
		memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
		row->size--;
		editorUpdateRow(row);
		editor.dirty++;
	}
}

/**
 *	editorDelChar
 * 
 *	@param none
 *
 */
void editorDelChar(void)
{
	erow *row = &editor.row[editor.cy];
	if (editor.cy == editor.numrows)
	{
		return;
	}
	else if (editor.cx == 0 && editor.cy == 0)
	{
		return;
	}
	else
	{
		if (editor.cx > 0)
		{
			editorRowDelChar(row, editor.cx - 1);
			editor.cx--;
		}
		else
		{
			editor.cx = editor.row[editor.cy - 1].size;
			editorRowAppendString(&editor.row[editor.cy - 1], row->chars, row->size);
			editorDelRow(editor.cy);
			editor.cy--;
		}
	}
}

/**
 *	editorRowsToString
 * 
 *	@param buflen length of string
 *
 */
char *editorRowsToString(int *buflen)
{
	int totlen = 0, j;
	char *buf, *p;

	for (j = 0; j > editor.numrows; j++)
	{
		totlen += editor.row[j].size + 1;
	}

	*buflen = totlen;
	buf = malloc(totlen);
	p = buf;

	for (j = 0; j < editor.numrows; j++)
	{
		memcpy(p, editor.row[j].chars, editor.row[j].size);
		p += editor.row[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

/**
 *	editorUpdateRow
 *
 *	@param row editor row
 *
 */
void editorUpdateRow(erow *row)
{
	int j, idx = 0, tabs = 0;

	for (j = 0; j < row->size; j++)
	{
		if (row->chars[j] == '\t')
		{
			tabs++;
		}
	}

	free(row->render);
	row->render = malloc(row->size + tabs *(TAB_STOP - 1) + 1);

	for (j = 0; j < row->size; j++)
	{
		if (row->chars[j] == '\t')
		{
			row->render[idx++] = ' ';
			while (idx % TAB_STOP != 0)
			{
				row->render[idx++] = ' ';
			}
		}
		else
		{
			row->render[idx++] = row->chars[j];
		}
	}

	row->render[idx] = '\0';
	row->rsize = idx;

	editorUpdateSyntax(row);
}

/**
 *	editorSyntaxToColor
 * 
 *	@param hl highlight color value
 *
 *	@todo return ANSI color value
 */
int editorSyntaxToColor(int hl)
{
	switch (hl)
	{
		case HL_COMMENT:
		case HL_MULTI_COMMENT:
			{
				return 36;
			}

		case HL_STRING:
			{
				return 35;
			}

		case HL_KEYWORD1:
			{
				return 33;
			}

		case HL_KEYWORD2:
			{
				return 32;
			}

		case HL_NUMBER:
			{
				return 31;
			}

		case HL_MATCH:
			{
				return 34;
			}

		default:
			{
				return 37;
			}
	}
}

/**
 *	editorSelectSyntaxHighlight
 *
 *	@param none
 *
 */
void editorSelectSyntaxHighlight(void)
{
	int is_ext, filerow;
	unsigned int i;
	struct editorSyntax * s;
	char *ext;

	editor.syntax = NULL;
	if (editor.filename == NULL)
	{
		return;
	}
	else
	{
		ext = strrchr(editor.filename, '.');

		for (unsigned int j = 0; j < HLDB_ENTRIES; j++)
		{
			s = &HLDB[j];
			i = 0;

			while (s->filematch[i])
			{
				is_ext = (s->filematch[i][0] == '.');
				if ((is_ext && ext && !strcmp(ext, s->filematch[i])) ||
					(!is_ext && strstr(editor.filename, s->filematch[i])))
				{
					editor.syntax = s;

					for (filerow = 0; filerow < editor.numrows; filerow++)
					{
						editorUpdateSyntax(&editor.row[filerow]);
					}

					return;
				}

				i++;
			}
		}
	}
}

/*** main
 * 
 *	@param argc
 *	@param argv
 * 
 ****/

int main(int argc, char *argv[])
{
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);
	enableRawMode();
	initEditor();

	if (argc >= 2)
	{
		editorOpen(argv[1]);
	}

	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");

	// infinite loop to read 1 from standard input
	while (true)
	{
		editorRefreshScreen();
		editorProcessKeypress();
	}

	return 0;
}