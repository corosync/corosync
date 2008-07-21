/*
 * Copyright (c) 2008 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Christine Caulfield (ccaulfie@redhat.com)
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

extern int confdb_sa_init(void);
extern int confdb_sa_object_create(unsigned int parent_object_handle, void *object_name, int object_name_len, unsigned int *object_handle);
extern int confdb_sa_object_destroy(unsigned int object_handle);
extern int confdb_sa_object_parent_get(unsigned int object_handle, unsigned int *parent_object_handle);
extern int confdb_sa_key_create(unsigned int parent_object_handle, void *key_name, int key_name_len, void *value, int value_len);
extern int confdb_sa_key_get(unsigned int parent_object_handle, void *key_name, int key_name_len, void *value, int *value_len);
extern int confdb_sa_key_replace(unsigned int parent_object_handle, void *key_name, int key_name_len, void *old_value, int old_value_len, void *new_value, int new_value_len);
extern int confdb_sa_object_find(unsigned int parent_object_handle, unsigned int start_pos, void *object_name, int object_name_len, unsigned int *object_handle, unsigned int *next_pos);
extern int confdb_sa_object_iter(unsigned int parent_object_handle, unsigned int start_pos, unsigned int *object_handle, void *object_name, int *object_name_len);
extern int confdb_sa_key_iter(unsigned int parent_object_handle, unsigned int start_pos, void *key_name, int *key_name_len, void *value, int *value_len);
extern int confdb_sa_write(char *error_text);
