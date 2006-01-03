/*
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * All rights reserved.
 *
 * Author: Steven Dake (sdake@mvista.com)
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
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>

struct timespec sleeptime = {
	.tv_sec = 0,
	.tv_nsec = 10000 /* 10 msec */
};

/*
 * The method by which the status is returned from execve
 * needs some performance enhancement
 */
int main (int argc, char **argv, char **envp)
{
	pid_t pid;
	pid_t res;
	int status;
	int i;

	pid = fork();
	if (pid == -1) {
		printf ("openais-instantiate: could not fork process %s\n", strerror (errno));
		return (errno);
	}
	if (pid) {
		/*
		 * Wait for a status code for at most 100 msec (10 * sleeptime)
		 * if child never returns a code, it is assumed to have been instantiated
		 */
		for (i = 0; i < 10; i++) {
			res = waitpid (pid, &status, WNOHANG);
			if (res) {
				if (WEXITSTATUS(status) == 0) {
					printf ("openais-instantiate: component instantiated\n");
					return (0);
				} else {
					printf ("openais-instantiate: could not instantiate component (return code %d = %s)\n",
						WEXITSTATUS(status),
						strerror (WEXITSTATUS(status)));
					return (errno);
				}
			}
			nanosleep (&sleeptime, 0);
			
		}
		printf ("openais-instantiate: component instantiated\n");
		return (0);
	} else {

printf ("childs pid %d\n", getpid());
		/*
		 * child process
		 */
		res = execve (argv[1], &argv[2], envp);
		if (res == -1) {
			return (errno);
		}
	}
	return (0);
}
