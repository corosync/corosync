/*
 * Copyright (c) 2002-2004 MontaVista Software, Inc.
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
/*
 * This implementation uses the same API as the linux kernel to
 * help us kernel developers easily use the list primatives
 */
#ifndef LIST_H_DEFINED
#define LIST_H_DEFINED

struct list_head {
	struct list_head *next;
	struct list_head *prev;
};

#define DECLARE_LIST_INIT(name) \
    struct list_head name = { &(name), &(name) }

static void inline list_init (struct list_head *head)
{
	head->next = head;
	head->prev = head;
}

static void inline list_add (struct list_head *new, struct list_head *head)
{
	head->next->prev = new;
	new->next = head->next;
	new->prev = head;
	head->next = new;
}
static void inline list_add_tail (struct list_head *new, struct list_head *head)
{
	head->prev->next = new;
	new->next = head;
	new->prev = head->prev;
	head->prev = new;
}
static void inline list_del (struct list_head *remove)
{
	remove->next->prev = remove->prev;
	remove->prev->next = remove->next;
#ifdef DEBUG
	remove->next = (struct list_head *)0xdeadb33f;
	remove->prev = (struct list_head *)0xdeadb33f;
#endif
}

#define list_entry(ptr,type,member)\
	((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

static inline int list_empty(struct list_head *l)
{
	return l->next == l;
}

#endif /* LIST_H_DEFINED */
