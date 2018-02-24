#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios og_termios;

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


int main() {

  enable_raw_mode();
  char c;

  while (1) {
    c = '\0';
    if (read(STDIN_FILENO, &c, 1) == -1 && \
        errno != EAGAIN)
          die("read");

    if (iscntrl(c)) 
      printf("%4d\r\n", c);
    else
      printf("%4d ('%c')\r\n", c, c);

    if (c == 'q') break;
  }    

  return 0;
}
