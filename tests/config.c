#include <assert.h>
#include "skippy.h"

bool debuglog = false;

// Section punctuation must preserve independent groups and their integer values.
int main(void)
{
	char path[] = "/tmp/skippy-config-XXXXXX";
	int fd = mkstemp(path);
	const char *text = "[label]\nfont=DejaVu Sans 11\n[ desktop-label ]\npadding=9\nmargin=12\nradius=15\n[test_section]\nvalue=ok\n";
	assert(fd >= 0);
	assert(write(fd, text, strlen(text)) == (ssize_t)strlen(text));
	close(fd);
	dlist *config = config_load(path);
	unlink(path);
	assert(!strcmp(config_get(config, "label", "font", ""), "DejaVu Sans 11"));
	assert(!config_get(config, "label", "padding", NULL));
	assert(config_get_int(config, "desktop-label", "padding", 0, 0, 256) == 9);
	assert(config_get_int(config, "desktop-label", "margin", 0, 0, 256) == 12);
	assert(config_get_int(config, "desktop-label", "radius", 0, 0, 100) == 15);
	assert(!strcmp(config_get(config, "test_section", "value", ""), "ok"));
	config_free(config);
	puts("Hyphenated/underscored sections and workspace label values passed.");
	return 0;
}
