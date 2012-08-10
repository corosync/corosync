/*
 * Copyright (c) 2008, 2012 Red Hat, Inc.
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
extern int confdb_sa_object_create(hdb_handle_t parent_object_handle,
				   const void *object_name,
				   size_t object_name_len,
				   hdb_handle_t *object_handle);
extern int confdb_sa_object_destroy(hdb_handle_t object_handle);
extern int confdb_sa_object_parent_get(hdb_handle_t object_handle,
				       hdb_handle_t *parent_object_handle);
extern int confdb_sa_object_name_get(hdb_handle_t object_handle,
				     char *object_name,
				     size_t *object_name_len);
extern int confdb_sa_key_create(hdb_handle_t parent_object_handle,
				const void *key_name,
				size_t key_name_len,
				const void *value,
				size_t value_len);
extern int confdb_sa_key_create_typed (hdb_handle_t parent_object_handle,
				const char *key_name,
				const void *value,
				size_t value_len,
				int type);
extern int confdb_sa_key_delete(hdb_handle_t parent_object_handle,
				const void *key_name,
				size_t key_name_len,
				const void *value,
				size_t value_len);
extern int confdb_sa_key_get(hdb_handle_t parent_object_handle,
			     const void *key_name,
			     size_t key_name_len,
			     void *value,
			     size_t *value_len);
extern int confdb_sa_key_get_typed(hdb_handle_t parent_object_handle,
			     const char *key_name,
			     void **value,
			     size_t *value_len,
			     int *type);
extern int confdb_sa_key_replace(hdb_handle_t parent_object_handle,
				 const void *key_name,
				 size_t key_name_len,
				 const void *old_value,
				 size_t old_value_len,
				 const void *new_value,
				 size_t new_value_len);
extern int confdb_sa_object_find(hdb_handle_t parent_object_handle,
				 hdb_handle_t *find_handle,
				 hdb_handle_t *object_handle,
				 const void *object_name,
				 size_t object_name_len);
extern int confdb_sa_object_iter(hdb_handle_t parent_object_handle,
				 hdb_handle_t *find_handle,
				 hdb_handle_t *object_handle,
				 const void *object_name,
				 size_t object_name_len,
				 void *found_object_name,
				 size_t *found_object_name_len);
extern int confdb_sa_key_iter(hdb_handle_t parent_object_handle,
			      hdb_handle_t start_pos,
			      void *key_name,
			      size_t *key_name_len,
			      void *value,
			      size_t *value_len);
extern int confdb_sa_key_iter_typed (hdb_handle_t parent_object_handle,
				hdb_handle_t start_pos,
				char *key_name,
				void **value,
				size_t *value_len,
				int *type);
extern int confdb_sa_key_increment(hdb_handle_t parent_object_handle,
				   const void *key_name,
				   size_t key_name_len,
				   unsigned int *value);
extern int confdb_sa_key_decrement(hdb_handle_t parent_object_handle,
				   const void *key_name,
				   size_t key_name_len,
				   unsigned int *value);
extern int confdb_sa_find_destroy(hdb_handle_t find_handle);
extern int confdb_sa_write(char *error_text, size_t errbuf_len);
extern int confdb_sa_reload(int flush, char *error_text, size_t errbuf_len);
