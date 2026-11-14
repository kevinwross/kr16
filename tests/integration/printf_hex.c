#include <stdio.h>
int main(void) {
  printf("%x %x %x %x %x %x %x %x -> %d=%x %s %c\n", 0, 1, 15, 16, 255,
         4095, 4660, -1, 255, 255, "hex", 'Z');
  return 0;
}

