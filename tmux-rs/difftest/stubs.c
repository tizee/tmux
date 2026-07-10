/*
 * Linker stubs for difftest: symbols grid.o/utf8.o reference but never call
 * on the tested paths (see docs/DEBUGGING.md section 4).
 */

#include <sys/types.h>

#include <stdlib.h>

#include "tmux.h"

time_t		 current_time;
struct options	*global_options;

int
colour_theme_terminal_colour(__unused u_int c)
{
	return (8);
}

void
colour_split_rgb(int c, u_char *r, u_char *g, u_char *b)
{
	*r = (c >> 16) & 0xff;
	*g = (c >> 8) & 0xff;
	*b = c & 0xff;
}

int
hyperlinks_get(__unused struct hyperlinks *hl, __unused u_int inner,
    __unused const char **uri_out, __unused const char **id_out,
    __unused const char **internal_id_out)
{
	return (0);
}

struct options_entry *
options_get(__unused struct options *oo, __unused const char *name)
{
	return (NULL);
}

struct options_array_item *
options_array_first(__unused struct options_entry *o)
{
	return (NULL);
}

struct options_array_item *
options_array_next(__unused struct options_array_item *a)
{
	return (NULL);
}

union options_value *
options_array_item_value(__unused struct options_array_item *a)
{
	return (NULL);
}
