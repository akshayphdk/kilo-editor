/*-----INCLUDES-----------------------------------------------*/

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

int get_window_size(int* rows, int* cols){
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || \
      ws.ws_col == 0){ return -1;}
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

void editor_draw_rows(){
  for(int y=0; y<CONF.screen_cols; y++){
    write(STDOUT_FILENO, "~\r\n", 3);
  }
}

void editor_refresh_screen(){
  write(STDOUT_FILENO, "\x1B[2J", 4);
  write(STDOUT_FILENO, "\x1B[H", 3);
  editor_draw_rows();
  write(STDOUT_FILENO, "\x1B[H", 3);
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
