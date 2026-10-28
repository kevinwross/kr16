int add(int a, int b) {
  int sum = a + b;
  return sum;
}

int main(void) {
  int i;
  int total = 0;
  for (i = 1; i <= 4; i++) {
    total = add(total, i);
  }
  printf("total=%d\n", total);
  return total;
}
