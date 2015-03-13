/*
 * On N files, set M attrs, of size S.
 */

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <attr/xattr.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <linux/limits.h>

static const char shortopts[] = "hvycdn:f:x:s:p:t:e:rk";
static const struct option longopts[] = {
	{ "help",	no_argument,		0,	'h' },
	{ "verbose",	no_argument,		0,	'v' },
	{ "verify",	no_argument,		0,	'y' },
	{ "nth",	required_argument,	0,	'n' },
	{ "files",	required_argument,	0,	'f' },
	{ "xattrs",	required_argument,	0,	'x' },
	{ "size",	required_argument,	0,	's' },
	{ "path",	required_argument,	0,	'p' },
	{ "synccaches", no_argument,		0,	'c' },
	{ "dropcaches",	no_argument,		0,	'd' },
	{ "script",	required_argument,	0,	't' },
	{ "seed",	required_argument,	0,	'e' },
	{ "random",	no_argument,		0,	'r' },
	{ "keep",	no_argument,		0,	'k' },
	{ 0,		0,			0,	0   }
};

static int verbose = 0;
static int verify = 0;
static int synccaches = 0;
static int dropcaches = 0;
static int nth = 0;
static int files = 1000;
static int xattrs = 1;
static int size  = 1;
static int size_is_random = 0;
static int keep_files = 0;
static char path[PATH_MAX] = "/tmp/xattrtest";
static char script[PATH_MAX] = "/bin/true";

static int
usage(int argc, char **argv) {
	fprintf(stderr,
	"usage: %s [-hvycdk] [-n <nth>] [-f <files>] [-x <xattrs>]\n"
	"       [-s <bytes>] [-p <path>] [-t <script> ]\n", argv[0]);
	fprintf(stderr,
	"  --help        -h           This help\n"
	"  --verbose     -v           Increase verbosity\n"
	"  --verify      -y           Verify xattr contents\n"
	"  --nth         -n <nth>     Print every nth file\n"
	"  --files       -f <files>   Set xattrs on N files\n"
	"  --xattrs      -x <xattrs>  Set N xattrs on each file\n"
	"  --size        -s <bytes>   Set N byters per xattr\n"
	"  --path        -p <path>    Path to files\n"
	"  --synccaches  -c           Sync caches between phases\n"
	"  --dropcaches  -d           Drop caches between phases\n"
	"  --script      -t <script>  Exec script between phases\n"
	"  --seed        -e <seed>    Random seed value\n"
	"  --random      -r           Randomly sized xattrs [16-size]\n"
	"  --keep        -k           Don't unlink files\n\n");

	return (0);
}

static int
parse_args(int argc, char **argv)
{
	long seed = time(NULL);
	int c;

	while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			return usage(argc, argv);
		case 'v':
			verbose++;
			break;
		case 'y':
			verify = 1;
			break;
		case 'n':
			nth = strtol(optarg, NULL, 0);
			break;
		case 'f':
			files = strtol(optarg, NULL, 0);
			break;
		case 'x':
			xattrs = strtol(optarg, NULL, 0);
			break;
		case 's':
			size = strtol(optarg, NULL, 0);
			break;
		case 'p':
			strncpy(path, optarg, PATH_MAX);
			break;
		case 'c':
			synccaches = 1;
			break;
		case 'd':
			dropcaches = 1;
			break;
		case 't':
			strncpy(script, optarg, PATH_MAX);
			break;
		case 'e':
			seed = strtol(optarg, NULL, 0);
			break;
		case 'r':
			size_is_random = 1;
			break;
		case 'k':
			keep_files = 1;
			break;
		default:
			fprintf(stderr, "Unknown option -%c\n", c);
			break;
		}
	}

	srandom(seed);

	if (verbose) {
		fprintf(stdout, "verbose:    %d\n", verbose);
		fprintf(stdout, "verify:     %d\n", verify);
		fprintf(stdout, "nth:        %d\n", nth);
		fprintf(stdout, "files:      %d\n", files);
		fprintf(stdout, "xattrs:     %d\n", xattrs);
		fprintf(stdout, "size:       %d\n", size);
		fprintf(stdout, "path:       %s\n", path);
		fprintf(stdout, "synccaches: %d\n", synccaches);
		fprintf(stdout, "dropcaches: %d\n", dropcaches);
		fprintf(stdout, "script:     %s\n", script);
		fprintf(stdout, "seed:       %ld\n", seed);
		fprintf(stdout, "random:     %d\n", size_is_random);
		fprintf(stdout, "keep:       %d\n", keep_files);
		fprintf(stdout, "%s", "\n");
	}

	return (0);
}

static int
drop_caches(void)
{
	char file[] = "/proc/sys/vm/drop_caches";
	int fd, rc;

	fd = open(file, O_WRONLY);
	if (fd == -1) {
		fprintf(stderr, "Error %d: open(\"%s\", O_WRONLY)\n",
			errno, file);
		return (errno);
	}

	rc = write(fd, "3", 1);
	if ((rc == -1) || (rc != 1)) {
		fprintf(stderr, "Error %d: write(%d, \"3\", 1)\n", fd, errno);
		return (errno);
	}

	rc = close(fd);
	if (rc == -1) {
		fprintf(stderr, "Error %d: close(%d)\n", errno, fd);
		return (errno);
	}

	return (0);
}

static int
run_process(const char *path, char *argv[])
{
	pid_t pid;
	int rc, devnull_fd;

	pid = vfork();
	if (pid == 0) {
		devnull_fd = open("/dev/null", O_WRONLY);

		if (devnull_fd < 0)
			_exit(-1);

		(void) dup2(devnull_fd, STDOUT_FILENO);
		(void) dup2(devnull_fd, STDERR_FILENO);
		close(devnull_fd);

		(void) execvp(path, argv);
		_exit(-1);
	} else if (pid > 0) {
		int status;

		while ((rc = waitpid(pid, &status, 0)) == -1 && errno == EINTR);

		if (rc < 0 || !WIFEXITED(status))
			return (-1);

		return WEXITSTATUS(status);
	}

	return (-1);
}

static int
post_hook(char *phase)
{
	char *argv[3] = { script, phase, (char *)0 };
	int rc;

	if (synccaches)
		sync();

	if (dropcaches) {
		rc = drop_caches();
		if (rc)
			return (rc);
	}

	rc = run_process(script, argv);
	if (rc)
		return (rc);

	return (0);
}

#define USEC_PER_SEC	1000000

static void
timeval_normalize(struct timeval *tv, time_t sec, suseconds_t usec)
{
	while (usec >= USEC_PER_SEC) {
		usec -= USEC_PER_SEC;
		sec++;
	}

	while (usec < 0) {
		usec += USEC_PER_SEC;
		sec--;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

static void
timeval_sub(struct timeval *delta, struct timeval *tv1, struct timeval *tv2)
{
	timeval_normalize(delta,
	    tv1->tv_sec - tv2->tv_sec,
	    tv1->tv_usec - tv2->tv_usec);
}

static int
create_files(void)
{
	int i, rc;
	char *file = NULL;
	struct timeval start, stop, delta;

	file = malloc(PATH_MAX);
	if (file == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for file name\n",
			rc, PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		(void) sprintf(file, "%s/file-%d", path, i);

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "create: %s\n", file);

		rc = unlink(file);
		if ((rc == -1)  && (errno != ENOENT)) {
			fprintf(stderr, "Error %d: unlink(%s)\n", errno, file);
			rc = errno;
			goto out;
		}

		rc = open(file, O_CREAT, 0644);
		if (rc == -1) {
			fprintf(stderr, "Error %d: open(%s, O_CREATE, 0644)\n",
				errno, file);
			rc = errno;
			goto out;
		}

		rc = close(rc);
		if (rc == -1) {
			fprintf(stderr, "Error %d: close(%d)\n", errno, rc);
			rc = errno;
			goto out;
		}
	}

	(void) gettimeofday(&stop, NULL);
	timeval_sub(&delta, &stop, &start);
	fprintf(stdout, "create:   %d.%d seconds\n",
	    (int)delta.tv_sec, (int)delta.tv_usec);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	return (rc);
}

static int
setxattrs(void)
{
	int i, j, rnd_size = size, shift, rc = 0;
	char name[XATTR_NAME_MAX];
	char *value = NULL;
	char *file = NULL;
	struct timeval start, stop, delta;

	value = malloc(XATTR_SIZE_MAX);
	if (value == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for xattr value\n",
			rc, XATTR_SIZE_MAX);
		goto out;
	}

	file = malloc(PATH_MAX);
	if (file == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for file name\n",
			rc, PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		(void) sprintf(file, "%s/file-%d", path, i);

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "setxattr: %s\n", file);

		for (j = 1; j <= xattrs; j++) {
			if (size_is_random)
				rnd_size = (random() % (size - 16)) + 16;

			(void) sprintf(name, "user.%d", j);
			shift = sprintf(value, "size=%d ", rnd_size);
			memset(value + shift, 'x', XATTR_SIZE_MAX - shift);

			rc = lsetxattr(file, name, value, rnd_size, 0);
			if (rc == -1) {
				fprintf(stderr, "Error %d: lsetxattr(%s, %s, "
				    "..., %d)\n", errno, file, name, rnd_size);
				goto out;
			}
		}
	}

	(void) gettimeofday(&stop, NULL);
	timeval_sub(&delta, &stop, &start);
	fprintf(stdout, "setxattr: %d.%d seconds\n",
	    (int)delta.tv_sec, (int)delta.tv_usec);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	if (value)
		free(value);

	return (rc);
}

static int
getxattrs(void)
{
	int i, j, rnd_size, shift, rc = 0;
	char name[XATTR_NAME_MAX];
	char *verify_value = NULL;
	char *value = NULL;
	char *file = NULL;
	struct timeval start, stop, delta;

	verify_value = malloc(XATTR_SIZE_MAX);
	if (verify_value == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for xattr verify\n",
			rc, XATTR_SIZE_MAX);
		goto out;
	}

	value = malloc(XATTR_SIZE_MAX);
	if (value == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for xattr value\n",
			rc, XATTR_SIZE_MAX);
		goto out;
	}

	file = malloc(PATH_MAX);
	if (file == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for file name\n",
			rc, PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		(void) sprintf(file, "%s/file-%d", path, i);

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "getxattr: %s\n", file);

		for (j = 1; j <= xattrs; j++) {
			(void) sprintf(name, "user.%d", j);

			rc = lgetxattr(file, name, value, XATTR_SIZE_MAX);
			if (rc == -1) {
				fprintf(stderr, "Error %d: lgetxattr(%s, %s, "
				    "..., %d)\n", errno, file, name,
				    XATTR_SIZE_MAX);
				goto out;
			}

			if (verify) {
				sscanf(value, "size=%d [a-z]", &rnd_size);
				shift = sprintf(verify_value, "size=%d ",
				    rnd_size);
				memset(verify_value + shift, 'x',
				    XATTR_SIZE_MAX - shift);

				if (rnd_size != rc ||
				    memcmp(verify_value, value, rnd_size)) {
					fprintf(stderr, "Error %d: verify "
					    "failed\nverify: %s\nvalue:  %s\n",
					    EINVAL, verify_value, value);
					goto out;
				}
			}
		}
	}

	(void) gettimeofday(&stop, NULL);
	timeval_sub(&delta, &stop, &start);
	fprintf(stdout, "getxattr: %d.%d seconds\n",
	    (int)delta.tv_sec, (int)delta.tv_usec);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	if (value)
		free(value);

	if (verify_value)
		free(verify_value);

	return (rc);
}

static int
unlink_files(void)
{
	int i, rc;
	char *file = NULL;
	struct timeval start, stop, delta;

	file = malloc(PATH_MAX);
	if (file == NULL) {
		rc = ENOMEM;
		fprintf(stderr, "Error %d: malloc(%d) bytes for file name\n",
			rc, PATH_MAX);
		goto out;
	}

	(void) gettimeofday(&start, NULL);

	for (i = 1; i <= files; i++) {
		(void) sprintf(file, "%s/file-%d", path, i);

		if (nth && ((i % nth) == 0))
			fprintf(stdout, "unlink: %s\n", file);

		rc = unlink(file);
		if ((rc == -1)  && (errno != ENOENT)) {
			fprintf(stderr, "Error %d: unlink(%s)\n", errno, file);
			return (errno);
		}
	}

	(void) gettimeofday(&stop, NULL);
	timeval_sub(&delta, &stop, &start);
	fprintf(stdout, "unlink:   %d.%d seconds\n",
	    (int)delta.tv_sec, (int)delta.tv_usec);

	rc = post_hook("post");
out:
	if (file)
		free(file);

	return (rc);
}

int
main(int argc, char **argv)
{
	int rc;

	rc = parse_args(argc, argv);
	if (rc)
		return (rc);

	rc = create_files();
	if (rc)
		return (rc);

	rc = setxattrs();
	if (rc)
		return (rc);

	rc = getxattrs();
	if (rc)
		return (rc);

	if (!keep_files) {
		rc = unlink_files();
		if (rc)
			return (rc);
	}

	return (0);
}
