/*
 * Copyright (c) 2004 MontaVista Software, Inc.
 * Copyright (c) 2005-2017 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@redhat.com)
 *         Jan Friesse (jfriesse@redhat.com)
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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <corosync/totem/totem.h>

#define DEFAULT_KEYFILE COROSYSCONFDIR "/authkey"

#define DEFAULT_KEYFILE_LEN		TOTEM_PRIVATE_KEY_LEN_MIN

#define DEFAULT_RANDOM_DEV		"/dev/random"

static const char usage[] =
	"Usage: corosync-keygen [-k <keyfile>] [-l] [-h]\n"
	"     -k / --key-file=<filename> -  Write to the specified keyfile\n"
	"            instead of the default " DEFAULT_KEYFILE ".\n"
	"     -l / --less-secure -  Use a less secure random number source\n"
	"            (/dev/urandom) that is guaranteed not to require user\n"
	"            input for entropy.  This can be used when this\n"
	"            application is used from a script.\n"
	"     -s / --size -  Length of key.\n"
	"     -h / --help -  Print basic usage.\n";


int main (int argc, char *argv[])
{
	int authkey_fd;
	int random_fd;
	char *keyfile = NULL;
	unsigned char key[TOTEM_PRIVATE_KEY_LEN_MAX];
	ssize_t res;
	ssize_t bytes_read;
	size_t key_len = DEFAULT_KEYFILE_LEN;
	const char *random_dev = DEFAULT_RANDOM_DEV;
	long long int tmpll;
	char *ep;
	int c;
	int option_index;
	int less_secure = 0;
	static struct option long_options[] = {
		{ "key-file",    required_argument, NULL, 'k' },
		{ "less-secure", no_argument,       NULL, 'l' },
		{ "size",        required_argument, NULL, 's' },
		{ "help",        no_argument,       NULL, 'h' },
		{ 0,             0,                 NULL, 0   },
	};

	while ((c = getopt_long (argc, argv, "k:s:lh",
			long_options, &option_index)) != -1) {
		switch (c) {
		case 'k':
			keyfile = optarg;
			break;
		case 'l':
			less_secure = 1;
			random_dev = "/dev/urandom";
			break;
		case 's':
			tmpll = strtoll(optarg, &ep, 10);
			if (tmpll < TOTEM_PRIVATE_KEY_LEN_MIN ||
			    tmpll > TOTEM_PRIVATE_KEY_LEN_MAX ||
			    errno != 0 || *ep != '\0') {
				printf ("Unsupported key size (supported <%u,%u>)\n",
				    TOTEM_PRIVATE_KEY_LEN_MIN,
				    TOTEM_PRIVATE_KEY_LEN_MAX);
				exit(1);
			}

			key_len = (size_t)tmpll;
			break;
		case 'h':
			printf ("%s\n", usage);
			exit(0);
			break;
		default:
			printf ("Error parsing command line options.\n");
			exit (1);
		}
	}

	printf ("Corosync Cluster Engine Authentication key generator.\n");

	if (!keyfile) {
		keyfile = (char *)DEFAULT_KEYFILE;
	}

	printf ("Gathering %lu bits for key from %s.\n", (unsigned long)(key_len * 8), random_dev);
	random_fd = open (random_dev, O_RDONLY);

	if (random_fd == -1) {
		err (1, "Failed to open random source");
	}

	if (!less_secure) {
		printf ("Press keys on your keyboard to generate entropy.\n");
	}
	/*
	 * Read random data
	 */
	bytes_read = 0;

retry_read:
	res = read (random_fd, &key[bytes_read], key_len - bytes_read);
	if (res == -1) {
		err (1, "Could not read /dev/random");
	}
	bytes_read += res;
	if (bytes_read != key_len) {
		printf ("Press keys on your keyboard to generate entropy (bits = %d).\n", (int)(bytes_read * 8));
		goto retry_read;
	}
	close (random_fd);

	/*
	 * Open key
	 */
	authkey_fd = open (keyfile, O_CREAT|O_WRONLY|O_TRUNC, 0600);
	if (authkey_fd == -1) {
		err (2, "Could not create %s", keyfile);
	}
	if (fchmod (authkey_fd, 0400)) {
		err (3, "Failed to set key file permissions to 0400");
	}

	printf ("Writing corosync key to %s.\n", keyfile);

	/*
	 * Write key
	 */
	res = write (authkey_fd, key, key_len);
	if (res != key_len) {
		err (4, "Could not write %s", keyfile);
	}

	if (close (authkey_fd)) {
		err (5, "Could not close %s", keyfile);
	}

	return (0);
}
