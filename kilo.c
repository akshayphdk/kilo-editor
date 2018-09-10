/*-----DEFINES------------------------------------------------*/

#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3
#define CTRL_KEY(k) ((k) & 0x1F)

/*-----INCLUDES-----------------------------------------------*/

#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>

/*-----DATA---------------------------------------------------*/

enum editor_keys {
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

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editor_config {
  int cx;
  int cy;
  int rx;
  int row_offset;
  int col_offset;
  int screen_rows;
  int screen_cols;
  int num_rows;
  erow *row;
  char *filename;
  char statusmsg[80];
  time_t statusmsg_time;
  int dirty;
  struct termios og_termios;
};

struct editor_config CONF;

/*-----APPEND BUFFER------------------------------------------*/

#define ABUF_INIT {NULL, 0}
struct abuf {
  char *b;
  int len;
};

void abuf_append(struct abuf* ab, const char* s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}
 
void abuf_free(struct abuf* ab){
  free(ab->b);
}

/*-----PROTOTYPES---------------------------------------------*/

void editor_set_statusmsg(const char *fmt, ...);

/*-----TERMINAL-----------------------------------------------*/

void die(const char* s){
  write(STDOUT_FILENO, "\x1B[2J", 4);
  write(STDOUT_FILENO, "\x1B[H", 3);
  perror(s);
  exit(1);
}

void disable_raw_mode(){
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &CONF.og_termios) == -1)
    die("tcsetattr - disable_raw_mode");
}

void enable_raw_mode(){
  if (tcgetattr(STDIN_FILENO, &CONF.og_termios) == -1)
    die("tcgetattr - enable_raw_mode");

  atexit(disable_raw_mode);

  struct termios raw = CONF.og_termios;
  raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr - enable_raw_mode");
}

int editor_read_key(){
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if (nread == -1 && errno != EAGAIN){
      die("read");
    }
  }

  if (c == '\x1B') {
    char seq[3];

    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1B';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1B';

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1B';
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
        }
      }
      else {
        switch(seq[1]){
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    }
    else if (seq[0] == 'O') {
      switch (seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1B';
  }
  else
    return c;
}

int get_cursor_position(int* rows, int* cols) {

  char buf[32];
  unsigned int i = 0;

  if(write(STDOUT_FILENO, "\x1B[6n", 4) != 4) return -1;

  while(i < sizeof(buf)-1) {
    if(read(STDIN_FILENO, &buf[i], 1) != 1) break;
    if(buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1B' || buf[1] != '[') return -1;
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int get_window_size(int* rows, int* cols){
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1B[999C\x1B[999B", 12) != 12)
      { return -1;}
    return get_cursor_position(rows, cols);
  }
  else {
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
  }
}

/*-----ROW OPERATIONS-----------------------------------------*/

int editor_row_cx_to_rx(erow *row, int cx) {
  int rx = 0;
  for (int j=0; j<cx; j++) {
    if (row->chars[j] == '\t')
      rx += (KILO_TAB_STOP-1)-(rx%KILO_TAB_STOP);
    rx++;
  }
  return rx;
}

void editor_update_row(erow *row){

  int tabs = 0;
  int j;
  for (j=0; j<row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size+(tabs*(KILO_TAB_STOP-1))+1);

  int idx = 0;
  for (j=0; j<row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx%KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    }
    else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editor_append_row(char *s, size_t len) {

  CONF.row = realloc(CONF.row, sizeof(erow)*(CONF.num_rows+1));

  int at = CONF.num_rows;
  CONF.row[at].size = len;
  CONF.row[at].chars = malloc(len+1);
  memcpy(CONF.row[at].chars, s, len);
  CONF.row[at].chars[len] = '\0';

  CONF.row[at].rsize = 0;
  CONF.row[at].render = NULL;
  editor_update_row(&CONF.row[at]);

  CONF.num_rows++;
  CONF.dirty++;
}

void editor_free_row(erow *row) {
  free(row->render);
  free(row->chars);
}

void editor_delete_row(int at) {
  if (at < 0 || at >= CONF.num_rows)
    return;
  editor_free_row(&CONF.row[at]);
  memmove(&CONF.row[at], &CONF.row[at+1], sizeof(erow)*(CONF.num_rows-at-1));
  CONF.num_rows--;
  CONF.dirty++;
}

void editor_row_insert_char(erow *row, int at, int c) {
  if (at < 0 || at > row->size)
    at = row->size;
  row->chars = realloc(row->chars, row->size+2);
  memmove(&row->chars[at+1], &row->chars[at], row->size-at+1);
  row->size++;
  row->chars[at] = c;
  editor_update_row(row);
  CONF.dirty++;
}

void editor_row_append_string(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size+len+1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editor_update_row(row);
  CONF.dirty++;
}

void editor_row_delete_char(erow *row, int at) {
  if (at < 0 || at >= row->size) 
    return;
  memmove(&row->chars[at], &row->chars[at+1], row->size-at);
  row->size--;
  editor_update_row(row);
  CONF.dirty++;
}

/*-----EDITOR OPERATIONS--------------------------------------*/

void editor_insert_char(int c) {
  if (CONF.cy == CONF.num_rows) {
    editor_append_row("", 0);
  }
  editor_row_insert_char(&CONF.row[CONF.cy], CONF.cx, c);
  CONF.cx++;
}

void editor_delete_char() {
  if (CONF.cy == CONF.num_rows)
    return;
  if (CONF.cx == 0 && CONF.cy == 0)
    return;

  erow *row = &CONF.row[CONF.cy];
  if (CONF.cx > 0) {
    editor_row_delete_char(row, CONF.cx-1);
    CONF.cx--;
  } 
  else {
    CONF.cx = CONF.row[CONF.cy-1].size;
    editor_row_append_string(&CONF.row[CONF.cy-1], row->chars, row->size);
    editor_delete_row(CONF.cy);
    CONF.cy--;
  }
}

/*-----FILE I/O-----------------------------------------------*/

char *editor_rows_to_string(int *buflen) {
  int totlen = 0;
  int j;
  for (j=0; j<CONF.num_rows; j++)
    totlen += CONF.row[j].size+1;
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for (j=0; j<CONF.num_rows; j++) {
    memcpy(p, CONF.row[j].chars, CONF.row[j].size);
    p += CONF.row[j].size;
    *p = '\n';
    p++;
  }
  return buf;
}

void editor_open(char *filename) {

  free(CONF.filename);
  CONF.filename = strdup(filename);
 
  FILE *fp = fopen(filename, "r");
  if (!fp) 
    die("fopen");

  char *line = NULL;
  size_t line_cap = 0;
  ssize_t line_len;

  while((line_len = getline(&line, &line_cap, fp)) != -1) {
    while (line_len > 0 && 
           (line[line_len-1] == '\n' || line[line_len-1] == '\r'))
      line_len--;
    editor_append_row(line, line_len);
  }
  free(line);
  fclose(fp);
  CONF.dirty = 0;
}

void editor_save() {
 if (CONF.filename == NULL) return;

  int len;
  char *buf = editor_rows_to_string(&len);
  int fd = open(CONF.filename, O_RDWR | O_CREAT, 0644);
  if (fd != -1) {
    if (ftruncate(fd, len) != -1) {
      if (write(fd, buf, len) == len) {
        close(fd);
        free(buf);
        CONF.dirty = 0;
        editor_set_statusmsg("%d bytes written to disk", len);
        return;
      }
    }
    close(fd);
  }
  free(buf);
  editor_set_statusmsg("Unable to save. I/O ErrorL %s", strerror(errno));
}

/*-----INPUT--------------------------------------------------*/

void editor_move_cursor(int key){

  erow *row = (CONF.cy >= CONF.num_rows)?NULL:&CONF.row[CONF.cy];

  switch(key) {
    case ARROW_LEFT:
      if (CONF.cx != 0)
        CONF.cx--;
      else if (CONF.cy > 0) {
        CONF.cy--;
        CONF.cx = CONF.row[CONF.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && CONF.cx < row->size)
        CONF.cx++;
      else if (row && CONF.cx == row->size) {
        CONF.cy++;
        CONF.cx = 0;
      }
      break;
    case ARROW_UP:
      if (CONF.cy != 0)
        CONF.cy--;
      break;
    case ARROW_DOWN:
      if (CONF.cy < CONF.num_rows)
        CONF.cy++;
      break;
  }

  row = (CONF.cy >= CONF.num_rows)?NULL:&CONF.row[CONF.cy];
  int row_len = row?row->size:0;
  if (CONF.cx > row_len) {
    CONF.cx = row_len;
  }

}

void editor_process_keypress(){

  static int quit_times = KILO_QUIT_TIMES;
  int c = editor_read_key();
  switch(c) {

    case '\r':
      /*TODO*/
      break;

    case CTRL_KEY('q'):
      if (CONF.dirty && quit_times > 0) {
        editor_set_statusmsg("Warning! File has unsaved changes." 
                             "Press Ctrl-Q %d more times to quit.", quit_times);
        quit_times--;
        return;
      }
      write(STDOUT_FILENO, "\x1B[2J", 4);
      write(STDOUT_FILENO, "\x1B[H", 3);
      exit(0);
      break;

    case CTRL_KEY('s'):
      editor_save();
      break;

    case HOME_KEY:
      CONF.cx = 0;
      break;

    case END_KEY:
      if (CONF.cy < CONF.num_rows)
        CONF.cx = CONF.row[CONF.cy].size;
      break;

    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY)
        editor_move_cursor(ARROW_RIGHT);
      editor_delete_char();
      break;

    case PAGE_UP:
    case PAGE_DOWN:
      {
        if (c == PAGE_UP) CONF.cy = CONF.row_offset;
        else if (c == PAGE_DOWN) {
          CONF.cy  = CONF.row_offset+CONF.screen_rows-1;
          if (CONF.cy > CONF.num_rows) CONF.cy = CONF.num_rows;
        }
        int times = CONF.screen_rows;
        while(times--)
          editor_move_cursor(c == PAGE_UP?ARROW_UP:ARROW_DOWN);  
      }
      break;

    case ARROW_UP:
    case ARROW_LEFT:
    case ARROW_DOWN:
    case ARROW_RIGHT:
      editor_move_cursor(c);
      break;

    case CTRL_KEY('l'):
    case '\x1B':
      break;  

    default:
      editor_insert_char(c);
      break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*-----OUTPUT-------------------------------------------------*/

void editor_scroll() {

  CONF.rx = CONF.cx;

  if (CONF.cy < CONF.num_rows) {
    CONF.rx = editor_row_cx_to_rx(&CONF.row[CONF.cy], CONF.cx);
  }
  if (CONF.cy < CONF.row_offset) {
    CONF.row_offset = CONF.cy;
  }
  if (CONF.cy >= CONF.row_offset+CONF.screen_rows){
    CONF.row_offset = CONF.cy-CONF.screen_rows+1;
  }
  if (CONF.rx < CONF.col_offset) {
    CONF.col_offset = CONF.rx;
  }
  if (CONF.rx >= CONF.col_offset+CONF.screen_cols){
    CONF.col_offset = CONF.rx-CONF.screen_cols+1;
  }
}

void editor_draw_rows(struct abuf* ab){
  for(int y=0; y<CONF.screen_rows; y++){
    int filerow = y + CONF.row_offset;
    if (filerow >= CONF.num_rows) {
      if (CONF.num_rows == 0 && y == CONF.screen_rows/2) {

        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome),
          "Kilo editor -- version %s", KILO_VERSION);
        if (welcomelen > CONF.screen_cols) 
          welcomelen = CONF.screen_cols;
        int padding = (CONF.screen_cols - welcomelen)/2;
        if (padding) {
          abuf_append(ab, "~", 1);
          padding--;
        }
        while (padding--) 
          abuf_append(ab, " ", 1);
        abuf_append(ab, welcome, welcomelen);
      }

      else
        abuf_append(ab, "~", 1);
    }
    else {
      int len = CONF.row[filerow].rsize-CONF.col_offset;
      if (len < 0) len = 0;
      if (len > CONF.screen_cols) len = CONF.screen_cols;
      abuf_append(ab, &CONF.row[filerow].render[CONF.col_offset], len);
    }

    abuf_append(ab, "\x1B[K", 3);
    abuf_append(ab, "\r\n", 2);
  }
}

void editor_draw_statusbar(struct abuf *ab) {
  abuf_append(ab, "\x1B[7m", 4);
  char status[80], rstatus[80];

  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
                     CONF.filename?CONF.filename:"[New File]", CONF.num_rows,
                     CONF.dirty?"(modified)":"");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d,%d", CONF.cy+1,CONF.cx+1);

  if (len > CONF.screen_cols)
    len = CONF.screen_cols;
  abuf_append(ab, status, len);

  while (len < CONF.screen_cols) {
    if (CONF.screen_cols - len == rlen) {
      abuf_append(ab, rstatus, rlen);
      break;
    }
    else {
      abuf_append(ab, " ", 1);
      len++;
    }
  }
  abuf_append(ab, "\x1B[m", 3);
  abuf_append(ab, "\r\n", 2);
}

void editor_draw_msgbar(struct abuf *ab) {
  abuf_append(ab, "\x1B[K", 3);
  int msglen = strlen(CONF.statusmsg);
  if (msglen > CONF.screen_cols)
    msglen = CONF.screen_cols;
  if (msglen && time(NULL) - CONF.statusmsg_time < 5)
    abuf_append(ab, CONF.statusmsg, msglen);
}

void editor_refresh_screen(){

  editor_scroll();

  struct abuf ab = ABUF_INIT;
 
  abuf_append(&ab, "\x1B[?25l", 6); 
  abuf_append(&ab, "\x1B[H", 3);

  editor_draw_rows(&ab);
  editor_draw_statusbar(&ab);
  editor_draw_msgbar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1B[%d;%dH", 
           CONF.cy-CONF.row_offset+1, CONF.rx-CONF.col_offset+1);
  abuf_append(&ab, buf, strlen(buf));

  abuf_append(&ab, "\x1B[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abuf_free(&ab);
}

void editor_set_statusmsg(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(CONF.statusmsg, sizeof(CONF.statusmsg), fmt, ap);
  va_end(ap);
  CONF.statusmsg_time = time(NULL);
}

/*-----INIT---------------------------------------------------*/

void init_editor(){
  CONF.cx = 0;
  CONF.cy = 0;
  CONF.rx = 0;
  CONF.row_offset = 0;
  CONF.col_offset = 0;
  CONF.num_rows = 0;
  CONF.row = NULL;
  CONF.filename = NULL;
  CONF.statusmsg[0] = '\0';
  CONF.statusmsg_time = 0;
  CONF.dirty = 0;
  if (get_window_size(&CONF.screen_rows,&CONF.screen_cols) == -1)
    die("get_window_size");
  CONF.screen_rows -= 2;
}

int main(int argc, char *argv[]) {

  enable_raw_mode();
  init_editor();
  if (argc >= 2) {
    editor_open(argv[1]);
  }

  editor_set_statusmsg("HELP: Ctrl-S = Save | Ctrl-Q = Quit");

  while (1) {
    editor_refresh_screen();
    editor_process_keypress();
  }    

  return 0;
}
