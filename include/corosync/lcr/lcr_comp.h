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

#ifndef LCR_COMP_H_DEFINED
#define LCR_COMP_H_DEFINED

/*
 * LCR Interface
 */
struct lcr_iface {
	const char *name;			/* Name of the interface */
	int version;			/* Version of this interface */
	int *versions_replace;		/* Versions that this interface can replace */
	int versions_replace_count;	/* Count of entries in version_replace */
	char **dependencies;		/* Dependent interfaces */
	size_t dependency_count;	/* Count of entires in dependencies */
	int (*constructor) (void *context);	/* Constructor for this interface */
	void (*destructor) (void *context);	/* Constructor for this interface */
	void **interfaces;		/* List of functions in interface */
};

/*
 * LCR Component
 */
struct lcr_comp {
	struct lcr_iface *ifaces;	/* List of interfaces in this component */
	size_t iface_count;		/* size of ifaces list */
};

extern void lcr_component_register (struct lcr_comp *comp);

static inline void lcr_interfaces_set (struct lcr_iface *iface, void *iface_list)
{
	iface->interfaces = (void **)iface_list;
}

#endif /* LCR_COMP_H_DEFINED */
