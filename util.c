/* IMPORTS */
#include <string.h>
#include <ctype.h>

/* FUNCTIONS */
int is_separator(int c) {
  return isspace(c) || c == '\0' || strchr(",.()+-/*=~%<>[];", c) != NULL;
}
