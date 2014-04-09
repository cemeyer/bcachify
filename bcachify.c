#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT(cond, fmt...)					\
	ASSERT_((uintptr_t)(cond), __func__, __LINE__,		\
		"ASSERTION: `" #cond "' failed " fmt)

/* The device to shift by BCACHE_SB_SPACE */
const char *DEVNAME = "XXX.123";

/* Minimum is 8kB (end of the BCache SB; my md array is chunked at 512k */
uint64_t BCACHE_SB_SPACE = UINT64_C(512*1024);
uint64_t resume_from = UINT64_MAX;

int logfd = -1;
int devfd = -1;

char *copybuf = NULL;

static void open_log(void);

static void
ASSERT_(uintptr_t cond, const char *func, unsigned line, const char *fmt, ...)
{
	va_list ap;

	if (cond)
		return;

	fprintf(stderr, "%s:%d: ", func, line);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	putc('\n', stderr);

	abort();
	exit(1);
}

static void
durable_log(const char *fmt, ...)
{
	static unsigned lines_written = 0;
	static char buf[1024];

	ssize_t wrt;
	va_list ap;
	int n;

	ASSERT(logfd >= 0);
	ASSERT(fmt[strlen(fmt) - 1] == '\n');

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);

	wrt = write(logfd, buf, n);
	ASSERT(wrt >= 0, "error: %d:%s", errno, strerror(errno));
	ASSERT(wrt == n);

	/* Occasionally rotate logs so we don't fill logging filesystem. */
	lines_written++;
	if (lines_written >= 10000) {
		struct stat sb;

		lines_written = 0;

		n = fstat(logfd, &sb);
		ASSERT(n == 0, "fstat: %d:%s", errno, strerror(errno));

		if (sb.st_size > 2*1024*1024) {
			n = fsync(logfd);
			ASSERT(n == 0, "fsync: %d:%s", errno, strerror(errno));

			close(logfd);
			logfd = -1;

			n = rename("bcachify.log", "bcachify.log.0");
			ASSERT(n == 0, "rename: %d:%s", errno, strerror(errno));

			open_log();
			n = fsync(logfd);
			ASSERT(n == 0, "fsync: %d:%s", errno, strerror(errno));
		}
	}
}

static void
open_log(void)
{
	logfd = open("bcachify.log", O_SYNC|O_APPEND|O_WRONLY|O_CREAT, 0644);
	ASSERT(logfd != -1);
	durable_log("================= Starting ==================\n");
	durable_log("Dev: %s SB_SPACE: %ju\n", DEVNAME,
			(uintmax_t)BCACHE_SB_SPACE);
}

static void
open_dev(void)
{
	devfd = open(DEVNAME, O_RDWR|O_SYNC|O_EXCL);
	ASSERT(devfd != -1, "Couldn't open %s: %s", DEVNAME, strerror(errno));
}

static uint64_t
dev_size(void)
{
	struct stat sb;
	off_t off;
	int rc;

	rc = fstat(devfd, &sb);
	ASSERT(rc != -1);

	if (sb.st_size)
		return sb.st_size;

	off = lseek(devfd, 0, SEEK_END);
	ASSERT(off > 0);

	(void) lseek(devfd, 0, SEEK_SET);

	return (uint64_t)off;
}

/*
 * Basically, durable (if not atomic): memmove(N, 0, M);
 *
 * N is BCACHE_SB_SPACE;
 * M is dev_size - N.
 *
 * Move in N-sized pieces for easy.
 *
 * Dev:
 * [ a | b | c | d ]
 *
 * Copy:
 *   d is trashed.
 *   src:                 dest:
 *   c (block total-2) to d (block total-1)
 *   b (block total-3) to c (block 2)
 *   a (block zero)    to b (block 1)
 */
static void
copy_end_to_front(uint64_t dev_size_bytes)
{
	uint64_t dest, src = UINT64_MAX;
	ssize_t n;

	dest = dev_size_bytes - BCACHE_SB_SPACE;
	if (resume_from != UINT64_MAX)
		dest = resume_from;

	for (; dest >= BCACHE_SB_SPACE; dest -= BCACHE_SB_SPACE) {
		src = dest - BCACHE_SB_SPACE;
		ASSERT(src < dest);

		durable_log("Copying %ju (block: %ju) to %ju (block %ju)\n",
			(uintmax_t)src, (uintmax_t)src/BCACHE_SB_SPACE,
			(uintmax_t)dest, (uintmax_t)dest/BCACHE_SB_SPACE);

		n = pread(devfd, copybuf, BCACHE_SB_SPACE, src);
		if (n < 0) {
			durable_log("ERROR pread: %d:%s\n", errno,
				strerror(errno));
			exit(2);
		}
		ASSERT((size_t)n == BCACHE_SB_SPACE, "short read");

		n = pwrite(devfd, copybuf, BCACHE_SB_SPACE, dest);
		if (n < 0) {
			durable_log("ERROR pwrite: %d:%s\n", errno,
				strerror(errno));
			exit(2);
		}
		ASSERT((size_t)n == BCACHE_SB_SPACE, "short write");
	}

	durable_log("=========== Finished copying at %ju->%ju =============\n",
		(uintmax_t)src, (uintmax_t)src+BCACHE_SB_SPACE);
}

static void
usage(void)
{
	printf("bcachify DEVICE [SB_SPACE]\n\n"
		"DEVICE - Device to insert bcache on. Must have at least\n"
		"\tSB_SPACE free space left at the end of the block device\n"
		"\tafter the filesystem. Use resize2fs or similar FIRST.\n"
		"SB_SPACE - Amount of room to leave for the bcache SB\n"
		"\t(in bytes). Minimum is 8k, default is 512k.\n");
	exit(0);
}

int
main(int argc, char **argv)
{
	uint64_t dev_end;

	if (argc < 2)
		usage();
	if (argv[1][0] == '-')
		usage();

	DEVNAME = argv[1];
	if (argc > 2)
		BCACHE_SB_SPACE = atoll(argv[2]);

	if (argc > 3) {
		resume_from = atoll(argv[3]);
		ASSERT(resume_from % BCACHE_SB_SPACE == 0);
	}

	ASSERT(BCACHE_SB_SPACE >= 8*1024);
	ASSERT(BCACHE_SB_SPACE % 512 == 0);

	open_log();
	open_dev();

	copybuf = malloc(BCACHE_SB_SPACE);
	ASSERT(copybuf, "oom");

	dev_end = dev_size();
	ASSERT(dev_end % BCACHE_SB_SPACE == 0);

	copy_end_to_front(dev_end);

	close(devfd);
	close(logfd);
	devfd = logfd = -1;

	printf("Okay, you're done migrating!\n");
	printf("First, invoke 'wipefs -a /dev/foo' to clear the few bytes\n"
	    "that identify this as a filesystem to blkid(1).\n\n");

	printf("Next, now invoke make-bcache like this:\n"
	    "make-bcache --bdev --data_offset %ju --block XXX "
		"[--cset-uuid UUID] /dev/foo\n\n",
		(uintmax_t)BCACHE_SB_SPACE/512);
	printf("(data_offset is the block size you passed earlier in units of\n"
	    " 512-byte sectors; the argument to --block must be the same you\n"
	    "used to set up the cache device, and is in bytes but must be a\n"
	    "multiple of 512-byte sectors and a power of two.)\n");

	fflush(stdout);

	return 0;
}
