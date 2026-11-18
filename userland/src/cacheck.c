/*
 * cacheck: what the root of the volume actually holds, and whether /System is reachable.
 *
 * /usr/lib/dyld opens and /System answers ENOENT, which is not the same as the directory not being there: the
 * volume was built with it.  This lists the root a name at a time, so the two can be told apart.
 */
#include "mini.h"

static const char *paths[] = {
	"/usr/lib/dyld",
	"/usr/share",
	"/System",
	"/Library",
	"/System/Library",
	NULL,
};

/* the directory entries in one run of getdirentries64() */
static void
list_root(void)
{
	static char buf[4096];
	struct dirent *d;
	long base = 0, n;
	int fd = open("/", O_RDONLY, 0);
	off_t position = 0;

	if (fd < 0) {
		printf("cacheck: open /: %s (%d)\n", strerror(errno), errno);
		return;
	}
	printf("cacheck: the root holds:");
	for (;;) {
		n = getdirentries(fd, buf, sizeof(buf), &position);
		if (n <= 0) {
			break;
		}
		for (base = 0; base < n; base += d->d_reclen) {
			d = (struct dirent *)(buf + base);
			if (d->d_namlen == 0 || d->d_name[0] == '.') {
				continue;
			}
			printf(" %s", d->d_name);
		}
	}
	printf("\n");
	close(fd);
}

int
main(int argc, char **argv, char **envp)
{
	char buf[24];

	(void)argc, (void)argv, (void)envp;

	list_root();
	for (int i = 0; paths[i] != NULL; i++) {
		int fd = open(paths[i], O_RDONLY, 0);

		if (fd < 0) {
			printf("cacheck: %-28s %s (%d)\n", paths[i], strerror(errno), errno);
			continue;
		}
		{
			ssize_t n = read(fd, buf, 16);

			printf("cacheck: %-28s ok (read %ld)\n", paths[i], (long)n);
		}
		close(fd);
	}
	return 0;
}
