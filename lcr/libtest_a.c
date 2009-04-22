/*
 * Copyright (C) 2006 Steven Dake (sdake@redhat.com)
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
#include <corosync/lcr/lcr_comp.h>

/*
 * Version 0 of the interface
 */
static int iface1_constructor (void *context);

static void iface1_destructor (void *context);

static void iface1_func1 (void);

static void iface1_func2 (void);

static void iface1_func3 (void);

/*
 * Version 1 of the interface
 */
static int iface1_ver1_constructor (void *context);

static void iface1_ver1_destructor (void *context);

static void iface1_ver1_func1 (void);

static void iface1_ver1_func2 (void);

static void iface1_ver1_func3 (void);

struct iface_list {
	void (*iface1_func1)(void);
	void (*iface1_func2)(void);
	void (*iface1_func3)(void);
};

struct iface_ver1_list {
	void (*iface1_ver1_func1)(void);
	void (*iface1_ver1_func2)(void);
	void (*iface1_ver1_func3)(void);
};

static struct iface_list iface_list = {
	.iface1_func1		= iface1_func1,
	.iface1_func2		= iface1_func2,
	.iface1_func3		= iface1_func3,
};

static struct iface_list iface_ver1_list = {
	.iface1_func1		= iface1_ver1_func1,
	.iface1_func2		= iface1_ver1_func2,
	.iface1_func3		= iface1_ver1_func3,
};

static struct lcr_iface iface1[2] = {
	/* version 0 */
	{
		.name			= "A_iface1",
		.version		= 0,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= iface1_constructor,
		.destructor		= iface1_destructor,
		.interfaces		= NULL
	},
	/* version 1 */
	{
		.name			= "A_iface1",
		.version		= 1,
		.versions_replace	= 0,
		.versions_replace_count	= 0,
		.dependencies		= 0,
		.dependency_count	= 0,
		.constructor		= iface1_ver1_constructor,
		.destructor		= iface1_ver1_destructor,
		.interfaces		= NULL
	}
};

static struct lcr_comp test_comp = {
	.iface_count 		= 2,
	.ifaces 		= iface1
};

static int iface1_constructor (void *context)
{
	printf ("A - version 0 constructor context %p\n", context);
	return (0);
}

static void iface1_destructor (void *context)
{
	printf ("A - version 0 destructor context %p\n", context);
}
static void iface1_func1 (void) {
	printf ("A - version 0 func1\n");
}

static void iface1_func2 (void) {
	printf ("A - version 0 func2\n");
}

static void iface1_func3 (void) {
	printf ("A - version 0 func3\n");
}

static int iface1_ver1_constructor (void *context)
{
	printf ("A - version 1 constructor context %p\n", context);
	return (0);
}

static void iface1_ver1_destructor (void *context)
{
	printf ("A - version 1 destructor context %p\n", context);
}
static void iface1_ver1_func1 (void) {
	printf ("A - version 1 func1\n");
}

static void iface1_ver1_func2 (void) {
	printf ("A - version 1 func2\n");
}

static void iface1_ver1_func3 (void) {
	printf ("A - version 1 func3\n");
}

__attribute__ ((constructor)) static void register_this_component (void) {
	lcr_interfaces_set (&iface1[0], &iface_list);
	lcr_interfaces_set (&iface1[1], &iface_ver1_list);
	lcr_component_register (&test_comp);
}
