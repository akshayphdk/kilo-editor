/*-----INCLUDES-----------------------------------------------*/

#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*-----DEFINES------------------------------------------------*/

#define CTRL_KEY(k) ((k) & 0x1F)

/*-----DATA---------------------------------------------------*/

struct editor_config {
  int screen_rows;
  int screen_cols;
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

char editor_read_key(){
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if (nread == -1 && errno != EAGAIN){
      die("read");
    }
  }
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
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || \
      ws.ws_col == 0) {
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

/*-----INPUT--------------------------------------------------*/

void editor_process_keypress(){
  char c = editor_read_key();
  switch(c) {
    case CTRL_KEY('q'):
      write(STDOUT_FILENO, "\x1B[2J", 4);
      write(STDOUT_FILENO, "\x1B[H", 3);
      exit(0);
  }
}

/*-----OUTPUT-------------------------------------------------*/

void editor_draw_rows(struct abuf* ab){
  for(int y=0; y<CONF.screen_rows; y++){
    abuf_append(ab, "~", 1);
    if (y<CONF.screen_rows-1){
      abuf_append(ab, "\r\n", 2);
    }
  }
}

void editor_refresh_screen(){
  struct abuf ab = ABUF_INIT;
 
  abuf_append(&ab, "\x1B[?25l", 6); 
  abuf_append(&ab, "\x1B[2J", 4);
  abuf_append(&ab, "\x1B[H", 3);

  editor_draw_rows(&ab);
  abuf_append(&ab, "\x1B[H", 3);
  abuf_append(&ab, "\x1B[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abuf_free(&ab);
}

/*-----INIT---------------------------------------------------*/

void init_editor(){
  if (get_window_size(&CONF.screen_rows,&CONF.screen_cols) == -1)
    die("get_window_size");
}

int main() {

  enable_raw_mode();
  init_editor();

  while (1) {
    editor_refresh_screen();
    editor_process_keypress();
  }    

  return 0;
}
