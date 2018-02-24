#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios og_termios;

void disable_raw_mode(){
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios);
}

void enable_raw_mode(){
  tcgetattr(STDIN_FILENO, &og_termios);
  atexit(disable_raw_mode);

  struct termios raw = og_termios;
  raw.c_iflag &= ~(BRKINT | INPCK | ISTRIP | ICRNL | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}


int main() {

  enable_raw_mode();
  char c;

  while (1) {
    c = '\0';
    read(STDIN_FILENO, &c, 1);

    if (iscntrl(c)) 
      printf("%4d\r\n", c);
    else
      printf("%4d ('%c')\r\n", c, c);

    if (c == 'q') break;
  }    

  return 0;
}
