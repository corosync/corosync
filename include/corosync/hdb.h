/*
 * Copyright (c) 2002-2006 MontaVista Software, Inc.
 * Copyright (c) 2006-2011 Red Hat, Inc.
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

#ifndef HDB_H_DEFINED
#define HDB_H_DEFINED

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <inttypes.h>
#include <qb/qbhdb.h>

typedef qb_handle_t hdb_handle_t;

/*
 * Formatting for string printing on 32/64 bit systems
 */
#define HDB_D_FORMAT QB_HDB_D_FORMAT
#define HDB_X_FORMAT QB_HDB_X_FORMAT

#define hdb_handle_database qb_hdb

/**
 * @brief hdb_database_lock
 * @param mutex
 */
static inline void hdb_database_lock (pthread_mutex_t *mutex)
{
	pthread_mutex_lock (mutex);
}

/**
 * @brief hdb_database_unlock
 * @param mutex
 */
static inline void hdb_database_unlock (pthread_mutex_t *mutex)
{
	pthread_mutex_unlock (mutex);
}

/**
 * @brief hdb_database_lock_init
 * @param mutex
 */
static inline void hdb_database_lock_init (pthread_mutex_t *mutex)
{
	pthread_mutex_init (mutex, NULL);
}

/**
 * @brief hdb_database_lock_destroy
 * @param mutex
 */
static inline void hdb_database_lock_destroy (pthread_mutex_t *mutex)
{
	pthread_mutex_destroy (mutex);
}

#define DECLARE_HDB_DATABASE QB_HDB_DECLARE

/**
 * @brief hdb_create
 * @param handle_database
 */
static inline void hdb_create (
	struct hdb_handle_database *handle_database)
{
	qb_hdb_create (handle_database);
}

/**
 * @brief hdb_destroy
 * @param handle_database
 */
static inline void hdb_destroy (
	struct hdb_handle_database *handle_database)
{
	qb_hdb_destroy (handle_database);
}

/**
 * @brief hdb_handle_create
 * @param handle_database
 * @param instance_size
 * @param handle_id_out
 * @return
 */
static inline int hdb_handle_create (
	struct hdb_handle_database *handle_database,
	int instance_size,
	hdb_handle_t *handle_id_out)
{
	return (qb_hdb_handle_create (handle_database, instance_size,
		handle_id_out));
}

/**
 * @brief hdb_handle_get
 * @param handle_database
 * @param handle_in
 * @param instance
 * @return
 */
static inline int hdb_handle_get (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in,
	void **instance)
{
	return (qb_hdb_handle_get (handle_database, handle_in, instance));
}

/**
 * @brief hdb_handle_get_always
 * @param handle_database
 * @param handle_in
 * @param instance
 * @return
 */
static inline int hdb_handle_get_always (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in,
	void **instance)
{
	return (qb_hdb_handle_get_always (handle_database, handle_in, instance));
}

/**
 * @brief hdb_handle_put
 * @param handle_database
 * @param handle_in
 * @return
 */
static inline int hdb_handle_put (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	return (qb_hdb_handle_put (handle_database, handle_in));
}

/**
 * @brief hdb_handle_destroy
 * @param handle_database
 * @param handle_in
 * @return
 */
static inline int hdb_handle_destroy (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	return (qb_hdb_handle_destroy (handle_database, handle_in));
}

/**
 * @brief hdb_handle_refcount_get
 * @param handle_database
 * @param handle_in
 * @return
 */
static inline int hdb_handle_refcount_get (
	struct hdb_handle_database *handle_database,
	hdb_handle_t handle_in)
{
	return (qb_hdb_handle_refcount_get (handle_database, handle_in));
}

/**
 * @brief hdb_iterator_reset
 * @param handle_database
 */
static inline void hdb_iterator_reset (
	struct hdb_handle_database *handle_database)
{
	qb_hdb_iterator_reset (handle_database);
}

/**
 * @brief hdb_iterator_next
 * @param handle_database
 * @param instance
 * @param handle
 * @return
 */
static inline int hdb_iterator_next (
	struct hdb_handle_database *handle_database,
	void **instance,
	hdb_handle_t *handle)
{
	return (qb_hdb_iterator_next (handle_database, instance, handle));
}

/**
 * @brief hdb_base_convert
 * @param handle
 * @return
 */
static inline unsigned int hdb_base_convert (hdb_handle_t handle)
{
	return (qb_hdb_base_convert (handle));
}

/**
 * @brief hdb_nocheck_convert
 * @param handle
 * @return
 */
static inline unsigned long long hdb_nocheck_convert (unsigned int handle)
{
	return (qb_hdb_nocheck_convert (handle));
}

#endif /* HDB_H_DEFINED */
