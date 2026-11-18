/*
 * cacheck: which directories of the root volume answer at all?
 *
 * /usr/lib/dyld opens and /System does not -- the whole tree under /System answers EIO -- and with it the dyld
 * shared cache, which is why dyld reports "no dyld cache" and every dynamically linked program dies.  This walks
 * the top of the volume so that the extent of it is known.
 */
#include "mini.h"

static const char *paths[] = {
	"/",
	"/bin",
	"/sbin",
	"/usr",
	"/usr/lib",
	"/usr/share",
	"/usr/local",
	"/private",
	"/dev",
	"/Applications",
	"/System",
	"/System/Library",
	"/System/Library/Frameworks",
	"/Library",
	"/redwood",
	NULL,
};

int
main(int argc, char **argv, char **envp)
{
	char buf[32];

	(void)argc, (void)argv, (void)envp;

	for (int i = 0; paths[i] != NULL; i++) {
		int fd = open(paths[i], O_RDONLY, 0);

		if (fd < 0) {
			printf("cacheck: %-32s %s (%d)\n", paths[i], strerror(errno), errno);
			continue;
		}
		{
			ssize_t n = read(fd, buf, 16);

			printf("cacheck: %-32s ok (read %ld)\n", paths[i], (long)n);
		}
		close(fd);
	}
	return 0;
}
