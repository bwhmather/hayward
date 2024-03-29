#ifndef HWD_STRINGOP_H
#define HWD_STRINGOP_H

#include <stdbool.h>
#include <stddef.h>

#include <hayward/list.h>

#ifdef __GNUC__
#define _HWD_ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _HWD_ATTRIB_PRINTF(start, end)
#endif

void
strip_whitespace(char *str);
void
strip_quotes(char *str);

// strcat that does nothing if dest or src is NULL
char *
lenient_strcat(char *dest, const char *src);
char *
lenient_strncat(char *dest, const char *src, size_t len);

// Simply split a string with delims, free with `list_free_items_and_destroy`
list_t *
split_string(const char *str, const char *delims);

// Splits an argument string, keeping quotes intact
char **
split_args(const char *str, int *argc);
void
free_argv(int argc, char **argv);

int
unescape_string(char *string);
char *
join_args(char **argv, int argc);

// Split string into 2 by delim, handle quotes
char *
argsep(char **stringp, const char *delim, char *matched_delim);

// Expand a path using shell replacements such as $HOME and ~
bool
expand_path(char **path);

#endif
