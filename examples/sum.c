int sum_to(int n) {
  int i;
  int total = 0;
  for (i = 1; i <= n; i++) {
    total = total + i;
  }
  return total;
}

int main(void) {
  return sum_to(10);
}
