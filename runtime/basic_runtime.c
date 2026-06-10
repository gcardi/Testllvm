#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *basic_strdup(const char *s) {
  if (!s) {
    s = "";
  }

  size_t n = strlen(s);
  char *copy = (char *)malloc(n + 1);
  if (!copy) {
    fputs("out of memory\n", stderr);
    exit(1);
  }

  memcpy(copy, s, n + 1);
  return copy;
}

int basic_input_i32(void) {
  int value = 0;
  if (scanf("%d", &value) != 1) {
    fputs("invalid integer input\n", stderr);
    exit(1);
  }
  return value;
}

char *basic_input_string(void) {
  char buffer[1024];
  if (!fgets(buffer, sizeof(buffer), stdin)) {
    return basic_strdup("");
  }

  size_t n = strlen(buffer);
  if (n > 0 && buffer[n - 1] == '\n') {
    buffer[n - 1] = '\0';
  }

  return basic_strdup(buffer);
}

char *basic_concat(const char *a, const char *b) {
  if (!a) {
    a = "";
  }
  if (!b) {
    b = "";
  }

  size_t an = strlen(a);
  size_t bn = strlen(b);
  char *out = (char *)malloc(an + bn + 1);
  if (!out) {
    fputs("out of memory\n", stderr);
    exit(1);
  }

  memcpy(out, a, an);
  memcpy(out + an, b, bn + 1);
  return out;
}

void basic_print_i32(int value) {
  printf("%d\n", value);
}

void basic_print_string(const char *value) {
  printf("%s\n", value ? value : "");
}
