/*** includes ***/

#include "kilo.h"

/*** defines ***/

#define KILO_VERSION "0.0.4"
#define CTRL_KEY(k) ((k) & 0x1F)
#define SHIFT_KEY(k) ((k) & 0x400)
#define ABUF_INIT {NULL, 0}
#define KILO_TAB_STOP 4
#define KILO_QUIT_TIMES 3
#define TAB_REPLACE 31

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

#define HLDB_ENTRIES (sizeof(HLDB) / sizeof(HLDB[0]))

/*** prototypes ***/

void editorSetStatusMessage(const char * fmt, ...);
void editorRefreshScreen();
char * editorPrompt(char * prompt, void (* callback)(char *, int));
void editorNewFile();

/*** data ***/

enum editorKey {
	BACKSPACE = 127,
	ARROW_LEFT = 0x200,
	ARROW_RIGHT,
	ARROW_UP,
	ARROW_DOWN,
	DEL_KEY,
	HOME_KEY,
	END_KEY,
	PAGE_UP,
	PAGE_DOWN,
	SHIFT_ARROW_LEFT = 0x400,
	SHIFT_ARROW_RIGHT,
	SHIFT_ARROW_UP,
	SHIFT_ARROW_DOWN,
	SHIFT_TAB
};

enum editorHighlight {
	HL_NORMAL = 0,
	HL_COMMENT,
	HL_MLCOMMENT,
	HL_KEYWORD1,
	HL_KEYWORD2,
	HL_STRING,
	HL_NUMBER,
	HL_MATCH,
	HL_SEL = 0,
	HL_UNSEL = 1
};

struct editorSyntax {
	char * filetype;
	char ** filematch;
	char ** keywords;
	char * singleline_comment_start;
	char * multiline_comment_start;
	char * multiline_comment_end;
	int flags;
};

typedef struct erow {
	int idx;
	int size;
	int rsize;
	char * chars;
	char * render;
	unsigned char * hl;
	unsigned char * sel;
	int hl_open_comment;
} erow;

typedef struct efile {
	int index;
	int cx, cy;
	int rx;
	int rowoff;
	int coloff;
	int numrows;
	erow * row;
	int dirty;
	int selected;
	char * filename;
	struct editorSyntax * syntax;
} efile;

struct editorConfig {
	int screenrows;
	int screencols;
	char * clipboard;
	int numfiles;
	int currentfile;
	efile * file;
	char statusmsg[80];
	time_t statusmsg_time;
	struct termios orig_termios;
};

struct abuf {
	char * b;
	int len;
};

void abAppend(struct abuf * ab, const char * s, int len) {
	char * new = realloc(ab->b, ab->len + len);
	if (new == NULL) return;
	memcpy(&new[ab->len], s, len);
	ab->b = new;
	ab->len += len;
}

void abFree(struct abuf * ab) {
	free(ab->b);
}

void editorFreeRow(erow * row);

struct editorConfig E;

struct editorSyntax HLDB[] = {
	{
		"C",
		C_HL_extensions,
		C_HL_keywords,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
	{
		"C++",
		CPP_HL_extensions,
		CPP_HL_keywords,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
	{
		"Python",
		PY_HL_extensions,
		PY_HL_keywords,
		"#", "'''", "'''",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	},
	{
		"JavaScript",
		JS_HL_extensions,
		JS_HL_keywords,
		"//", "/*", "*/",
		HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
	}
};

/*** terminal ***/

void die(const char * s) {
	for (int i = 0; i < E.numfiles; i++) {
		efile * F = &E.file[i];
		if (F) {
			for (int j = 0; j < F->numrows; j++) editorFreeRow(&F->row[j]);
			free(F);
		}
	}
	
	write(STDOUT_FILENO, "\x1b[2J", 4);
	write(STDOUT_FILENO, "\x1b[H", 3);

	perror(s);
	exit(1);
}

void disableRawMode() {
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) die("tcsetattr");
}

void enableRawMode() {
	if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) die("tcgetattr");
	atexit(disableRawMode);
	struct termios raw = E.orig_termios;
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST);
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) die("tcsetattr");
}

int editorReadKey() {
	int nread;
	char c;
	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) die("read");
	}

	if (c == '\x1b') {
		char seq[5];
		if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
		if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return HOME_KEY;
						case '3': return DEL_KEY;
						case '4': return END_KEY;
						case '5': return PAGE_UP;
						case '6': return PAGE_DOWN;
						case '7': return HOME_KEY;
						case '8': return END_KEY;
					}
				} else if (seq[1] == '1' && seq[2] == ';') {
					if (read(STDIN_FILENO, &seq[3], 1) != 1) return '\x1b';
					if (read(STDIN_FILENO, &seq[4], 1) != 1) return '\x1b';
					if (seq[3] == '2') {
						switch (seq[4]) {
							//case 'A': return SHIFT_ARROW_UP;
							//case 'B': return SHIFT_ARROW_DOWN;
							case 'C': return SHIFT_ARROW_RIGHT;
							case 'D': return SHIFT_ARROW_LEFT;
						}
					}
				}
			} else {
				switch (seq[1]) {
					case 'A': return ARROW_UP;
					case 'B': return ARROW_DOWN;
					case 'C': return ARROW_RIGHT;
					case 'D': return ARROW_LEFT;
					case 'H': return HOME_KEY;
					case 'F': return END_KEY;
					case 'Z': return SHIFT_TAB;
				}
			}
		} else if (seq[0] == 'O') {
			switch (seq[1]) {
				case 'H': return HOME_KEY;
				case 'F': return END_KEY;
			}
		}
		return '\x1b';
	}
	return c;
}

int getCursorPosition(int *rows, int * cols) {
	char buf[32];
	unsigned int i = 0;

	if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

	while (i < sizeof(buf) - 1) {
		if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
		if (buf[i] == 'R') break;
		i++;
	}

	buf[i] = '\0';
	if (buf[0] != '\x1b' || buf[1] != '[') return -1;
	if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

	return 0;
}

int getWindowSize(int *rows, int * cols, int force) {
	struct winsize ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		if (!force || write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
		return getCursorPosition(rows, cols);
	} else {
		*cols = ws.ws_col;
		*rows = ws.ws_row - 2;
		return 0;
	}
}

/*** syntax highlighting ***/

int is_separator(int c) {
	return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}

void removeHighlight() {
	efile * F = &E.file[E.currentfile];
	for (int i = 0; i < F->numrows; i++) {
		erow * row = &F->row[i];
		row->sel = realloc(row->sel, row->rsize);
		memset(row->sel, HL_UNSEL, row->rsize);
	}
	F->selected = 0;
}

void editorUpdateSyntax(erow *row) {
	efile * F = &E.file[E.currentfile];
	row->hl = realloc(row->hl, row->rsize);
	memset(row->hl, HL_NORMAL, row->rsize);
	row->sel = realloc(row->sel, row->rsize);
	memset(row->sel, HL_UNSEL, row->rsize);
	
	if (F->syntax == NULL) return;

	char ** keywords = F->syntax->keywords;
	char * scs = F->syntax->singleline_comment_start;
	char * mcs = F->syntax->multiline_comment_start;
	char * mce = F->syntax->multiline_comment_end;
	int scs_len = scs ? strlen(scs) : 0;
	int mcs_len = mcs ? strlen(mcs) : 0;
	int mce_len = mce ? strlen(mce) : 0;

	int prev_sep = 1;
	int in_string = 0;
	int in_comment = (row->idx > 0 && F->row[row->idx - 1].hl_open_comment);
	
	int i = 0;
	while (i < row->rsize) {
		char c = row->render[i];
		unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : HL_NORMAL;

		if (scs_len && !in_string && !in_comment) {
			if (!strncmp(&row->render[i], scs, scs_len)) {
				memset(&row->hl[i], HL_COMMENT, row->rsize - i);
				break;
			}
		}
		
		if (mcs_len && mce_len && !in_string) {
			if (in_comment) {
				row->hl[i] = HL_MLCOMMENT;
				if (!strncmp(&row->render[i], mce, mce_len)) {
					memset(&row->hl[i], HL_MLCOMMENT, mce_len);
					i += mce_len;
					in_comment = 0;
					prev_sep = 1;
					continue;
				} else {
					i++;
					continue;
				}
			} else if (!strncmp(&row->render[i], mcs, mcs_len)) {
				memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
				i += mcs_len;
				in_comment = 1;
				continue;
			}
		}
		
		if (F->syntax->flags & HL_HIGHLIGHT_STRINGS) {
			if (in_string) {
				row->hl[i] = HL_STRING;
				if (c == '\\' && i + 1 < row->rsize) {
					row->hl[i + 1] = HL_STRING;
					i += 2;
					continue;
				}
				if (c == in_string) in_string = 0;
				i++;
				prev_sep = 1;
				continue;
			} else {
				if (c == '"' || c == '\'') {
					in_string = c;
					row->hl[i] = HL_STRING;
					i++;
					continue;
				}
			}
		}
		
		if (F->syntax->flags & HL_HIGHLIGHT_NUMBERS) {
			if ((isdigit(c) && (prev_sep || prev_hl == HL_NUMBER)) || (c == '.' && prev_hl == HL_NUMBER)) {
				row->hl[i] = HL_NUMBER;
				i++;
				prev_sep = 0;
				continue;
			}
		}

		if (prev_sep) {
			int j;
			for (j = 0; keywords[j]; j++) {
				int klen = strlen(keywords[j]);
				int kw2 = keywords[j][klen - 1] == '|';
				if (kw2) klen--;

				if (!strncmp(&row->render[i], keywords[j], klen) && is_separator(row->render[i + klen])) {
					memset(&row->hl[i], kw2 ? HL_KEYWORD2 : HL_KEYWORD1, klen);
					i += klen;
					break;
				}
			}
			if (keywords[j] != NULL) {
				prev_sep = 0;
				continue;
			}
		}

		prev_sep = is_separator(c);
		i++;
	}

	int changed = (row->hl_open_comment != in_comment);
	row->hl_open_comment = in_comment;
	if (changed && row->idx + 1 < F->numrows) editorUpdateSyntax(&F->row[row->idx + 1]);
}

int editorSyntaxToColor(int hl) {
	switch (hl) {
		case HL_COMMENT:
		case HL_MLCOMMENT: return 36;
		case HL_KEYWORD1: return 33;
		case HL_KEYWORD2: return 32;
		case HL_STRING: return 35;
		case HL_NUMBER: return 31;
		case HL_MATCH: return 34;
		default: return 37;
	}
}

void editorSelectSyntaxHighlight() {
	efile * F = &E.file[E.currentfile];
	F->syntax = NULL;
	if (F->filename == NULL) return;
	
	char * ext = strrchr(F->filename, '.');

	for (unsigned int j = 0; j < HLDB_ENTRIES; j++) {
		struct editorSyntax * s = &HLDB[j];
		unsigned int i = 0;
		while (s->filematch[i]) {
			int is_ext = (s->filematch[i][0] == '.');
			if ((is_ext && ext && !strcmp(ext, s->filematch[i])) || (!is_ext && strstr(F->filename, s->filematch[i]))) {
				F->syntax = s;
				for (int filerow = 0; filerow < F->numrows; filerow++) editorUpdateSyntax(&F->row[filerow]);
				return;
			}
			i++;
		}
	}
}

/*** row operations ***/

int editorRowCxToRx(erow * row, int cx) {
	int rx = 0;
	for (int j = 0; j < cx; j++) {
		if (row->chars[j] == '\t') rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP);
		rx++;
	}
	return rx;
}

int editorRowRxToCx(erow * row, int rx) {
	int cur_rx = 0;
	int cx;
	for (cx = 0; cx < row->size; cx++) {
		if (row->chars[cx] == '\t') cur_rx += (KILO_TAB_STOP - 1) - (cur_rx % KILO_TAB_STOP);
		cur_rx++;
		if (cur_rx > rx) return cx;
	}
	return cx;
}

void editorUpdateRow(erow * row) {
	int tabs = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') tabs++;
	}

	free(row->render);
	row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

	int idx = 0;
	for (int j = 0; j < row->size; j++) {
		if (row->chars[j] == '\t') {
			row->render[idx++] = ' ';
			while (idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
		} else row->render[idx++] = row->chars[j];
	}
	row->render[idx] = '\0';
	row->rsize = idx;

	editorUpdateSyntax(row);
}

void editorInsertRow(int at, char * s, size_t len) {
	efile * F = &E.file[E.currentfile];
	if (at < 0 || at > F->numrows) return;

	F->row = realloc(F->row, sizeof(erow) * (F->numrows + 1));
	memmove(&F->row[at + 1], &F->row[at], sizeof(erow) * (F->numrows - at));
	for (int j = at + 1; j <= F->numrows; j++) F->row[j].idx++;

	F->row[at].idx = at;

	F->row[at].size = len;
	F->row[at].chars = malloc(len + 1);
	memcpy(F->row[at].chars, s, len);
	F->row[at].chars[len] = '\0';

	F->row[at].rsize = 0;
	F->row[at].render = NULL;
	F->row[at].hl = NULL;
	F->row[at].sel = NULL;
	F->row[at].hl_open_comment = 0;
	editorUpdateRow(&F->row[at]);

	F->numrows++;
	F->dirty++;
}

void editorFreeRow(erow * row) {
	free(row->render);
	free(row->chars);
	free(row->hl);
}

void editorFreeFile(efile * F) {
	int index = F->index;
	for (int i = 0; i < F->numrows; i++) editorFreeRow(&F->row[i]);
	if (index >= 0 && index < E.numfiles)
		memmove(&E.file[index], &E.file[index + 1], sizeof(efile) * (E.numfiles - index - 1));
	for (int i = index; i < E.numfiles - 1; i++) E.file[i].index--;
	E.numfiles--;
	if (E.numfiles > 0) E.currentfile = 0;
	else editorNewFile();
}

void editorDelRow(int at) {
	efile * F = &E.file[E.currentfile];
	if (at < 0 || at >= F->numrows) return;
	editorFreeRow(&F->row[at]);
	memmove(&F->row[at], &F->row[at + 1], sizeof(erow) * (F->numrows - at - 1));
	for (int j = at; j < F->numrows - 1; j++) F->row[j].idx--;	

	F->numrows--;
	F->dirty++;
	if (F->selected) removeHighlight();
}

void editorRowInsertChar(erow * row, int at, int c) {
	efile * F = &E.file[E.currentfile];
	if (at < 0 || at > row->size) at = row->size;
	row->chars = realloc(row->chars, row->size + 2);
	memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
	row->size++;
	row->chars[at] = c;
	editorUpdateRow(row);
	F->dirty++;
	if (F->selected) removeHighlight();
}

void editorRowAppendString(erow * row, char * s, size_t len) {
	efile * F = &E.file[E.currentfile];
	row->chars = realloc(row->chars, row->size + len + 1);
	memcpy(&row->chars[row->size], s, len);
	row->size += len;
	row->chars[row->size] = '\0';
	editorUpdateRow(row);
	F->dirty++;
	if (F->selected) removeHighlight();
}

void editorRowDelChar(erow * row, int at) {
	efile * F = &E.file[E.currentfile];
	if (at < 0 || at > row->size) return;
	memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
	row->size--;
	editorUpdateRow(row);
	F->dirty++;
	if (F->selected) removeHighlight();
}

/*** editor operations ***/

void editorInsertChar(int c) {
	efile * F = &E.file[E.currentfile];
	if (F->cy == F->numrows) {
		editorInsertRow(F->numrows, "", 0);
	}
	editorRowInsertChar(&F->row[F->cy], F->cx, c);
	F->cx++;
}

void editorInsertNewline() {
	efile * F = &E.file[E.currentfile];
	int indent = 0;
	if (F->cx == 0) {
		editorInsertRow(F->cy, "", 0);
	} else {
		erow * row = &F->row[F->cy];
		editorInsertRow(F->cy + 1, &row->chars[F->cx], row->size - F->cx);
		row = &F->row[F->cy];
		row->size = F->cx;
		row->chars[row->size] = '\0';
		while (row->chars[indent] == ' ' || row->chars[indent] == '\t') {
			editorRowInsertChar(&F->row[F->cy + 1], indent, row->chars[indent]);
			indent++;
		}
		editorUpdateRow(row);
	}
	F->cy++;
	F->cx = indent;
}

void editorCopyChars() {
	efile * F = &E.file[E.currentfile];
	if (!F->selected) return;
	
	if (E.clipboard) {
		free(E.clipboard);
		E.clipboard = NULL;
	}
	
	int len = 1;
	int line = -1;
	for (int i = 0; i < F->numrows; i++)
		for (int j = 0; j < F->row[i].size; j++)
			if (!F->row[i].sel[j]) {
				if (line < 0) line = i;
				else if (line != i) {
					line = i;
					len++;
				}
				len++;
			}
	
	E.clipboard = malloc(len);
	int index = 0;
	line = -1;
	for (int i = 0; i < F->numrows; i++) {
		for (int j = 0; j < F->row[i].size; j++) {
			if (!F->row[i].sel[j]) {
				if (line < 0) line = i;
				else if (line != i) {
					E.clipboard[index++] = '\n';
					line = i;
				}
				E.clipboard[index++] = (F->row[i].chars[j] != '\t') ? F->row[i].render[j] : TAB_REPLACE;
			}
		}
	}
	E.clipboard[index] = '\0';
	editorSetStatusMessage("Copied item to clipboard");
}

void editorPasteChars() {
	efile * F = &E.file[E.currentfile];
	if (E.clipboard == NULL) return;
	
	char * c = E.clipboard;
	while (*c) {
		if (*c == '\n') editorInsertRow(F->cy, "", 0);
		else if (*c == TAB_REPLACE) editorInsertChar('\t');
		else editorInsertChar(*c);
		c++;
	}
}

void editorDuplicateRow() {
	efile * F = &E.file[E.currentfile];
	if (F->cy == F->numrows) return;
	erow * row = &F->row[F->cy];
	editorInsertRow(F->cy + 1, row->chars, row->size);
	F->cy++;
}

void editorDeleteRow() {
	efile * F = &E.file[E.currentfile];
	if (F->cy == F->numrows) return;
	editorDelRow(F->cy);
}

void editorDelChar() {
	efile * F = &E.file[E.currentfile];
	if (F->cy == F->numrows) return;
	if (F->cx == 0 && F->cy == 0) return;

	erow * row = &F->row[F->cy];
	if (F->cx > 0) {
		editorRowDelChar(row, F->cx - 1);
		F->cx--;
	} else {
		F->cx = F->row[F->cy - 1].size;
		editorRowAppendString(&F->row[F->cy - 1], row->chars, row->size);
		editorDelRow(F->cy);
		F->cy--;
	}
}

/*** file i/o ***/

char * editorRowsToString(int * buflen) {
	efile * F = &E.file[E.currentfile];
	int totlen = 0;
	for (int j = 0; j < F->numrows; j++)
		totlen += F->row[j].size + 1;
	*buflen = totlen;

	char * buf = malloc(totlen);
	char * p = buf;
	for (int j = 0; j < F->numrows; j++) {
		memcpy(p, F->row[j].chars, F->row[j].size);
		p += F->row[j].size;
		*p = '\n';
		p++;
	}
	return buf;
}

void editorNewFile() {
	efile F;
	
	F.index = E.numfiles;
	F.cx = 0;
	F.cy = 0;
	F.rx = 0;
	F.rowoff = 0;
	F.coloff = 0;
	F.numrows = 0;
	F.row = NULL;
	F.dirty = 0;
	F.selected = 0;
	F.filename = NULL;
	F.syntax = NULL;
	
	E.file = realloc(E.file, sizeof(efile) * (E.numfiles + 1));
	memcpy(&E.file[E.numfiles], &F, sizeof(efile));
	E.currentfile = E.numfiles;
	E.numfiles++;
}

void editorOpen(char * filename) {
	FILE * fp = fopen(filename, "r");
	if (!fp) {
		editorSetStatusMessage("Could not open file %s", filename); // die("fopen");
		return;
	}
	editorNewFile();
	efile * F = &E.file[E.currentfile];
	F->filename = strdup(filename);
	editorSelectSyntaxHighlight();
	
	char * line = NULL;
	size_t linecap = 0;
	ssize_t linelen;
	while ((linelen = getline(&line, &linecap, fp)) != -1) {
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
		editorInsertRow(F->numrows, line, linelen);
	}
	free(line);
	fclose(fp);
	F->dirty = 0;
}

void editorSave() {
	efile * F = &E.file[E.currentfile];
	if (F->filename == NULL) {
		F->filename = editorPrompt("Save as: %s", NULL);
		if (F->filename == NULL) {
			editorSetStatusMessage("Save aborted");
			return;
		}
		editorSelectSyntaxHighlight();
	}

	int len;
	char * buf = editorRowsToString(&len);

	int fd = open(F->filename, O_RDWR | O_CREAT, 0644);
	if (fd != -1) {
		if (ftruncate(fd, len) != -1) {
			if (write(fd, buf, len) == len) {
				close(fd);
				free(buf);
				F->dirty = 0;
				editorSetStatusMessage("%d bytes written to disk", len);
				return;
			}
		}
		close(fd);
	}
	free(buf);
	editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
}

void editorNextFile() {
	E.currentfile = (E.currentfile + 1) % E.numfiles;
}

/*** find ***/

void editorFindCallback(char * query, int key) {
	static int last_match = -1;
	static int direction = 1;

	static int saved_hl_line;
	static char * saved_hl = NULL;

	efile * F = &E.file[E.currentfile];

	if (saved_hl) {
		memcpy(F->row[saved_hl_line].hl, saved_hl, F->row[saved_hl_line].rsize);
		free(saved_hl);
		saved_hl = NULL;
	}

	if (key == '\r' || key == '\x1b') {
		last_match = -1;
		direction = 1;
		return;
	} else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
		direction = 1;
	} else if (key == ARROW_LEFT || key == ARROW_UP) {
		direction = -1;
	} else {
		last_match = -1;
		direction = 1;
	}

	if (last_match == -1) direction = 1;
	int current = last_match;
	for (int i = 0; i < F->numrows; i++) {
		current += direction;
		if (current == -1) current = F->numrows - 1;
		else if (current == F->numrows) current = 0;		

		erow * row = &F->row[current];
		char * match = strcasestr(row->render, query);
		if (match) {
			last_match = current;
			F->cy = current;
			F->cx = editorRowRxToCx(row, match - row->render);
			F->rowoff = F->numrows;

			saved_hl_line = current;
			saved_hl = malloc(row->rsize);
			memcpy(saved_hl, row->hl, row->rsize);
			memset(&row->hl[match - row->render], HL_MATCH, strlen(query));
			break;
		}
	}
}

void editorFind() {
	efile * F = &E.file[E.currentfile];
	int saved_cx = F->cx;
	int saved_cy = F->cy;
	int saved_coloff = F->coloff;
	int saved_rowoff = F->rowoff;

	char * query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)", editorFindCallback);
	if (query) free(query);
	else {
		F->cx = saved_cx;
		F->cy = saved_cy;
		F->coloff = saved_coloff;
		F->rowoff = saved_rowoff;
	}
}

/*** input ***/

char * editorPrompt(char * prompt, void (* callback)(char *, int)) {
	size_t bufsize = 128;
	char * buf = malloc(bufsize);
	
	size_t buflen = 0;
	buf[0] = '\0';

	while (1) {
		editorSetStatusMessage(prompt, buf);
		editorRefreshScreen();

		int c = editorReadKey();
		if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {		
			if (buflen != 0) buf[--buflen] = '\0';
		} else if (c == '\x1b') {
			editorSetStatusMessage("");
			if (callback) callback(buf, c);
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editorSetStatusMessage("");
				if (callback) callback(buf, c);
				return buf;
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen == bufsize - 1) {
				bufsize *= 2;
				buf = realloc(buf, bufsize);
			}
			buf[buflen++] = c;
			buf[buflen] = '\0';
		}

		if (callback) callback(buf, c);
	}
}

void editorMoveCursor(int key) {
	efile * F = &E.file[E.currentfile];
	erow * row = (F->cy >= F->numrows) ? NULL : &F->row[F->cy];
	
	/* highlighting */
	if (SHIFT_KEY(key) && row->size > 0) {
		if (key == SHIFT_ARROW_RIGHT) row->sel[F->rx] = !row->sel[F->rx];
		else if (SHIFT_ARROW_LEFT && F->rx > 0) row->sel[F->rx - 1] = !row->sel[F->rx - 1];
		F->selected = 1;
	} else removeHighlight();
	
	/* moving */
	switch (key) {
		case ARROW_LEFT:
		case SHIFT_ARROW_LEFT:
			if (F->cx != 0) F->cx--;
			else if (F->cy > 0) {
				F->cy--;
				F->cx = F->row[F->cy].size;
			}
			break;
			
		case ARROW_RIGHT:
		case SHIFT_ARROW_RIGHT:
			if (row && F->cx < row->size) F->cx++;
			else if (row && F->cx == row->size) {
				F->cy++;
				F->cx = 0;
			}
			break;
			
		case ARROW_UP:
		case SHIFT_ARROW_UP:
			if (F->cy != 0) F->cy--;
			break;
			
		case ARROW_DOWN:
		case SHIFT_ARROW_DOWN:
			if (F->cy < F->numrows) F->cy++;
			break;
			
		case HOME_KEY:
			F->cx = 0;
			break;
			
		case END_KEY:
			if (F->cy < F->numrows) F->cx = F->row[F->cy].size;
			break;
	}

	row = (F->cy >= F->numrows) ? NULL : &F->row[F->cy];
	int rowlen = row ? row->size : 0;
	if (F->cx > rowlen) F->cx = rowlen;
}

void editorProcessKeypress() {
	efile * F = &E.file[E.currentfile];
	
	static int quit_times = KILO_QUIT_TIMES;
	int c = editorReadKey();

	switch (c) {
		case '\r':
			editorInsertNewline();
			break;

		case CTRL_KEY('q'):
			if (F->dirty && quit_times > 0) {
				editorSetStatusMessage("WARNING!!! File has unsaved changes. Press Ctrl-Q %d more times to quit.", quit_times);
				quit_times--;
				return;
			}
			if (E.numfiles == 1) {
				write(STDOUT_FILENO, "\x1b[2J", 4);
				write(STDOUT_FILENO, "\x1b[H", 3);
				exit(0);
			} else {
				editorFreeFile(F); 
			}
			break;

		case CTRL_KEY('s'):
			editorSave();
			break;
		
		case CTRL_KEY('o'):
			{
				char * filename = editorPrompt("Open file: %s", NULL);
				editorOpen(filename);
				break;
			}
		
		case CTRL_KEY('n'):
			editorNewFile();
			break;
						
		case CTRL_KEY('f'):
			editorFind();
			break;
		
		case CTRL_KEY('d'):
			editorDuplicateRow();
			break;
		
		case CTRL_KEY('k'):
			editorDeleteRow();
			break;
			
		case CTRL_KEY('c'):
			editorCopyChars();
			break;
			
		case CTRL_KEY('v'):
			editorPasteChars();
			break;
		
		case PAGE_UP:
		case PAGE_DOWN:
			{
				if (c == PAGE_UP) {
					F->cy = F->rowoff;
				} else if (c == PAGE_DOWN) {
					F->cy = F->rowoff + E.screenrows - 1;
					if (F->cy > F->numrows) F->cy = F->numrows;
				}

				int times = E.screenrows;
				while (times--) editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
			}
			break;

		case BACKSPACE:
		case CTRL_KEY('h'):
		case DEL_KEY:
			if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
			editorDelChar();
			break;

		case CTRL_KEY('l'):
		case '\x1b':
			break;

		case ARROW_UP:
		case ARROW_DOWN:
		case ARROW_LEFT:
		case ARROW_RIGHT:
		case SHIFT_ARROW_UP:
		case SHIFT_ARROW_DOWN:
		case SHIFT_ARROW_LEFT:
		case SHIFT_ARROW_RIGHT:
		case HOME_KEY:
		case END_KEY:
			editorMoveCursor(c);
			break;

		case SHIFT_TAB:
			editorNextFile();
			break;

		default:
		editorInsertChar(c);
		break;
	}

	quit_times = KILO_QUIT_TIMES;
}

/*** output ***/

void editorScroll() {
	efile * F = &E.file[E.currentfile];
	int numlen = (int) ceil(log10(F->numrows + 1));	
	F->rx = 0;
	if (F->cy < F->numrows) {
		F->rx = editorRowCxToRx(&F->row[F->cy], F->cx);
	}

	if (F->cy < F->rowoff) {
		F->rowoff = F->cy;
	}
	if (F->cy >= F->rowoff + E.screenrows) {
		F->rowoff = F->cy - E.screenrows + 1;
	}
	if (F->rx < F->coloff) {
		F->coloff = F->rx;
	}
	if (F->rx >= F->coloff + E.screencols - (numlen + 2)) {
		F->coloff = F->rx - E.screencols + numlen + 3;
	}
}

void editorDrawRows(struct abuf *ab) {
	efile * F = &E.file[E.currentfile];	
	int numlen = (int) ceil(log10(F->numrows + 1));
	char * linenum = malloc(numlen + 1);
	for (int y = 0; y < E.screenrows; y++) {
		int filerow = y + F->rowoff;
		if (filerow >= F->numrows) {
			if (F->numrows == 0 && y == E.screenrows / 3) {
				char welcome[80];
				int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor -- version %s", KILO_VERSION);
				if (welcomelen > E.screencols) welcomelen = E.screencols;
				int padding = (E.screencols - welcomelen) / 2;
				if (padding) {
					abAppend(ab, "~", 1);
					padding--;
				}
				while (padding--) abAppend(ab, " ", 1);
				abAppend(ab, welcome, welcomelen);
			} else {
				abAppend(ab, "~", 1);
			}
		} else {
			snprintf(linenum, numlen + 3, "%*d| ", numlen, filerow + 1);
			abAppend(ab, linenum, numlen + 3);
			int len = F->row[filerow].rsize - F->coloff;
			if (len < 0) len = 0;
			if (len > E.screencols - (numlen + 2)) len = E.screencols - (numlen + 2);
			char * c = &F->row[filerow].render[F->coloff];
			unsigned char * hl = &F->row[filerow].hl[F->coloff];
			unsigned char * sel = &F->row[filerow].sel[F->coloff];
			int current_color = -1;
			for (int j = 0; j < len; j++) {
				if (iscntrl(c[j])) {
					char sym = (c[j] <= 26) ? '@' + c[j] : '?';
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &sym, 1);
					abAppend(ab, "\x1b[m", 3);
					if (current_color != -1) {
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", current_color);
						abAppend(ab, buf, clen);
					}
				} else if (sel[j] == HL_SEL) {
					current_color = -1;
					abAppend(ab, "\x1b[7m", 4);
					abAppend(ab, &c[j], 1);
					abAppend(ab, "\x1b[m", 3);
				} else if (hl[j] == HL_NORMAL) {
					if (current_color != -1) {
						abAppend(ab, "\x1b[39m", 5);
						current_color = -1;
					}
					abAppend(ab, &c[j], 1);
				} else {
					int color = editorSyntaxToColor(hl[j]);
					if (color != current_color) {
						current_color = color;
						char buf[16];
						int clen = snprintf(buf, sizeof(buf), "\x1b[%dm", color);
						abAppend(ab, buf, clen);
					}
					abAppend(ab, &c[j], 1);
				}
			}
			abAppend(ab, "\x1b[39m", 5);
		}
		abAppend(ab, "\x1b[K\r\n", 5);		
	}
}

void editorDrawStatusBar(struct abuf * ab) {
	efile * F = &E.file[E.currentfile];
	abAppend(ab, "\x1b[7m", 4);
	char status[80], rstatus[80];
	int len = snprintf(status, sizeof(status), "%.20s file (%d/%d) %s", F->filename ? F->filename : "[No Name]", E.currentfile + 1, E.numfiles, F->dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%s | %d/%d", F->syntax ? F->syntax->filetype : "no ft", F->cy + 1, F->numrows);
	if (len > E.screencols) len = E.screencols;
	abAppend(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			abAppend(ab, rstatus, rlen);
			break;
		} else {
			abAppend(ab, " ", 1);
			len++;
		}
	}
	abAppend(ab, "\x1b[m\r\n", 5);
}

void editorDrawMessageBar(struct abuf * ab) {
	abAppend(ab, "\x1b[K", 3);
	int msglen = strlen(E.statusmsg);
	if (msglen > E.screencols) msglen = E.screencols;
	if (msglen && time(NULL) - E.statusmsg_time < 5) abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen() {
	efile * F = &E.file[E.currentfile];
	struct abuf ab = ABUF_INIT;
	getWindowSize(&E.screenrows, &E.screencols, 0);
	
	editorScroll();
	abAppend(&ab, "\x1b[?25l", 6);
	abAppend(&ab, "\x1b[H", 3);
	editorDrawRows(&ab);
	editorDrawStatusBar(&ab);
	editorDrawMessageBar(&ab);

	char buf[32];
	int numlen = (int) ceil(log10(F->numrows + 1));
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (F->cy - F->rowoff) + 1, (F->rx - F->coloff) + numlen + 3);
	abAppend(&ab, buf, strlen(buf));
	abAppend(&ab, "\x1b[?25h", 6);

	write(STDOUT_FILENO, ab.b, ab.len);
	abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

/*** init ***/

void initEditor() {
	E.numfiles = 0;
	E.currentfile = -1;
	E.clipboard = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;

	if (getWindowSize(&E.screenrows, &E.screencols, 1) == -1) die("getWindowSize");
}

int main(int argc, char * argv[]) {
	enableRawMode();
	initEditor();
	if (argc >= 2) {
		editorOpen(argv[1]);
	} else {
		editorNewFile();
	}	
	
	editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-F = find | Ctrl-Q = quit");

	while (1) {
		editorRefreshScreen();
		editorProcessKeypress();
	}
	
	return 0;
}
