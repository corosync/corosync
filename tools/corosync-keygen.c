/*
 * Copyright (c) 2004 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *
 * This software licensed under BSD license, the text of which follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the name of the MontaVista Software, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <netinet/in.h>

#define KEYFILE COROSYSCONFDIR "/authkey"

int main (void) {
	int authkey_fd;
	int random_fd;
	unsigned char key[128];
	ssize_t res;
	ssize_t bytes_read;

	printf ("Corosync Cluster Engine Authentication key generator.\n");
	if (geteuid() != 0) {
		printf ("Error: Authorization key must be generated as root user.\n");
		exit (errno);
	}
	if (mkdir (COROSYSCONFDIR, 0700)) {
		if (errno != EEXIST) {
			perror ("Failed to create directory: " COROSYSCONFDIR);
			exit (errno);
		}
	}

	printf ("Gathering %lu bits for key from /dev/random.\n", (unsigned long)(sizeof (key) * 8));
	printf ("Press keys on your keyboard to generate entropy.\n");
	random_fd = open ("/dev/random", O_RDONLY);
	if (random_fd == -1) {
		perror ("Is /dev/random present? Opening /dev/random");
		exit (errno);
	}

	/*
	 * Read random data
	 */
	bytes_read = 0;

retry_read:
	res = read (random_fd, &key[bytes_read], sizeof (key) - bytes_read);
	if (res == -1) {
		perror ("Could not read /dev/random");
		exit (errno);
	}
	bytes_read += res;
	if (bytes_read != sizeof (key)) {
		printf ("Press keys on your keyboard to generate entropy (bits = %d).\n", (int)(bytes_read * 8));
		goto retry_read;
	}
	close (random_fd);

	/*
	 * Open key
	 */
	authkey_fd = open (KEYFILE, O_CREAT|O_WRONLY, 600);
	if (authkey_fd == -1) {
		perror ("Could not create " KEYFILE);
		exit (errno);
	}
	/*
	 * Set security of authorization key to uid = 0 gid = 0 mode = 0400
	 */
	res = fchown (authkey_fd, 0, 0);
	if (res == -1) {
		perror ("Could not fchown key to uid 0 and gid 0\n");
		exit (errno);
	}
	if (fchmod (authkey_fd, 0400)) {
		perror ("Failed to set key file permissions to 0400\n");
		exit (errno);
	}

	printf ("Writing corosync key to " KEYFILE ".\n");

	/*
	 * Write key
	 */
	res = write (authkey_fd, key, sizeof (key));
	if (res != sizeof (key)) {
		perror ("Could not write " KEYFILE);
		exit (errno);
	}

	if (close (authkey_fd)) {
		perror ("Could not write " KEYFILE);
		exit (errno);
	}

	return (0);
}
