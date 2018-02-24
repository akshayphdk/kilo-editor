/*-----INCLUDES-----------------------------------------------*/

#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*-----DEFINES------------------------------------------------*/

#define CTRL(k) ((k) & 0x1F)

/*-----DATA---------------------------------------------------*/

struct termios og_termios;

/*-----TERMINAL-----------------------------------------------*/

void die(const char* s){
  perror(s);
  exit(1);
}

void disable_raw_mode(){
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1)
    die("tcsetattr - disable_raw_mode");
}

void enable_raw_mode(){
  if (tcgetattr(STDIN_FILENO, &og_termios) == -1)
    die("tcgetattr - enable_raw_mode");

  atexit(disable_raw_mode);

  struct termios raw = og_termios;
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

/*-----INPUT--------------------------------------------------*/

void editor_process_keypress(){
  char c = editor_read_key();
  switch(c) {
    case CTRL('q'):
      exit(0);
  }
}

/*-----OUTPUT-------------------------------------------------*/

void editor_refresh_screen(){
  write(STDOUT_FILENO, "\x1B[2J", 4);
}

/*-----INIT---------------------------------------------------*/

int main() {

  enable_raw_mode();
  char c;

  while (1) {
    editor_refresh_screen();
    editor_process_keypress();
  }    

  return 0;
}
