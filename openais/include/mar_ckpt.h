/*
 * Copyright (C) 2006 Red Hat, Inc.
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

#ifndef AIS_MAR_CKPT_H_DEFINED
#define AIS_MAR_CKPT_H_DEFINED

#include <corosync/mar_gen.h>
#include "saAis.h"
#include "saCkpt.h"

typedef mar_uint64_t mar_ckpt_checkpoint_handle_t;

static inline void swab_mar_ckpt_checkpoint_handle_t (
	mar_ckpt_checkpoint_handle_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

typedef mar_uint32_t mar_ckpt_checkpoint_creation_flags_t;

static inline void swab_mar_ckpt_checkpoint_creation_flags_t (
	mar_ckpt_checkpoint_creation_flags_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * Marshalling the SaCkptCheckpointCreationAttributesT data structure
 */
typedef struct {
	mar_ckpt_checkpoint_creation_flags_t creation_flags __attribute__((aligned(8)));
	mar_size_t checkpoint_size __attribute__((aligned(8)));
	mar_time_t retention_duration __attribute__((aligned(8)));
	mar_uint32_t max_sections __attribute__((aligned(8)));
	mar_size_t max_section_size __attribute__((aligned(8)));
	mar_size_t max_section_id_size __attribute__((aligned(8)));
} mar_ckpt_checkpoint_creation_attributes_t;

static inline void swab_mar_ckpt_checkpoint_creation_attributes_t (
	mar_ckpt_checkpoint_creation_attributes_t *to_swab)
{
	swab_mar_ckpt_checkpoint_creation_flags_t (&to_swab->creation_flags);
	swab_mar_size_t (&to_swab->checkpoint_size);
	swab_mar_time_t (&to_swab->retention_duration);
	swab_mar_uint32_t (&to_swab->max_sections);
	swab_mar_size_t (&to_swab->max_section_size);
	swab_mar_size_t (&to_swab->max_section_id_size);
}

static inline void marshall_from_mar_ckpt_checkpoint_creation_attributes_t (
	SaCkptCheckpointCreationAttributesT *dest,
	mar_ckpt_checkpoint_creation_attributes_t *src)
{
	dest->creationFlags = src->creation_flags;
	dest->checkpointSize = src->checkpoint_size;
	dest->retentionDuration = src->retention_duration;
	dest->maxSections = src->max_sections;
	dest->maxSectionSize = src->max_section_size;
	dest->maxSectionIdSize = src->max_section_id_size;
}

static inline void marshall_to_mar_ckpt_checkpoint_creation_attributes_t (
	mar_ckpt_checkpoint_creation_attributes_t *dest,
	SaCkptCheckpointCreationAttributesT *src)
{
	memset (dest, 0, sizeof (mar_ckpt_checkpoint_creation_attributes_t));
	dest->creation_flags = src->creationFlags;
	dest->checkpoint_size = src->checkpointSize;
	dest->retention_duration = src->retentionDuration;
	dest->max_sections = src->maxSections;
	dest->max_section_size = src->maxSectionSize;
	dest->max_section_id_size = src->maxSectionIdSize;
}
	
#define MAR_CKPT_CHECKPOINT_READ	0x1
#define MAR_CKPT_CHECKPOINT_WRITE	0x2
#define MAR_CKPT_CHECKPOINT_CREATE	0x4

typedef mar_uint32_t mar_ckpt_checkpoint_open_flags_t;

static inline void swab_mar_ckpt_checkpoint_open_flags_t (mar_ckpt_checkpoint_open_flags_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

#define MAR_CKPT_DEFAULT_SECTION_ID	{ 0, 0 }
#define MAR_CKPT_GENERATED_SECTION_ID	{ 0, 0 }

/*
 * Marshalling the SaCkptSectionIdT data structure
 */
typedef struct {
	mar_uint16_t id_len __attribute__((aligned(8)));
	mar_uint8_t *id __attribute__((aligned(8)));
} mar_ckpt_section_id_t;

static inline void swab_mar_ckpt_section_id_t (mar_ckpt_section_id_t *to_swab)
{
	swab_mar_uint16_t (&to_swab->id_len);
}

static inline void marshall_from_mar_ckpt_section_id_t (
	SaCkptSectionIdT *dest,
	mar_ckpt_section_id_t *src)
{
	dest->idLen = src->id_len;
	dest->id = src->id;
}

static inline void marshall_to_mar_ckpt_section_id_t (
	mar_ckpt_section_id_t *dest,
	SaCkptSectionIdT *src)
{
	memset (dest, 0, sizeof (mar_ckpt_section_id_t));
	dest->id_len = src->idLen;
	dest->id = src->id;
}

/*
 * Marshalling the SaCkptSectionCreationAttributesT data structure
 */
typedef struct {
	mar_ckpt_section_id_t *section_id __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
} mar_ckpt_section_creation_attributes_t;

static inline void swab_mar_ckpt_section_creation_attributes_t (
	mar_ckpt_section_creation_attributes_t *to_swab)
{
	swab_mar_ckpt_section_id_t (to_swab->section_id);
	swab_mar_time_t (&to_swab->expiration_time);
}

static inline void marshall_from_mar_ckpt_section_creation_attributes_t (
	SaCkptSectionCreationAttributesT *dest,
	mar_ckpt_section_creation_attributes_t *src)
{
	marshall_from_mar_ckpt_section_id_t (dest->sectionId, src->section_id);
	dest->expirationTime = src->expiration_time;
}

static inline void marshall_to_mar_ckpt_section_creation_attributes_t (
	mar_ckpt_section_creation_attributes_t *dest,
	SaCkptSectionCreationAttributesT *src)
{
	memset (dest, 0, sizeof (mar_ckpt_section_creation_attributes_t));
	marshall_to_mar_ckpt_section_id_t (dest->section_id, src->sectionId);
	dest->expiration_time = src->expirationTime;
}

typedef mar_uint32_t mar_ckpt_section_state_t;
#define	MAR_CKPT_SECTION_VALID 1
#define	MAR_CKPT_SECTION_CORRUPTED 2

static inline void swab_mar_ckpt_section_state_t (
	mar_ckpt_section_state_t *to_swab)
{
	swab_mar_uint32_t (to_swab);
}

/*
 * Marshalling the SaCkptSectionDescriptorT data structure
 */
typedef struct {
	mar_ckpt_section_id_t section_id __attribute__((aligned(8)));
	mar_time_t expiration_time __attribute__((aligned(8)));
	mar_size_t section_size __attribute__((aligned(8)));
	mar_ckpt_section_state_t section_state __attribute__((aligned(8)));
	mar_time_t last_update __attribute__((aligned(8)));
} mar_ckpt_section_descriptor_t;

static inline void swab_mar_ckpt_section_descriptor_t (
	mar_ckpt_section_descriptor_t *to_swab)
{
	swab_mar_ckpt_section_id_t (&to_swab->section_id);
	swab_mar_time_t (&to_swab->expiration_time);
	swab_mar_size_t (&to_swab->section_size);
	swab_mar_ckpt_section_state_t (&to_swab->section_state);
	swab_mar_time_t (&to_swab->last_update);
}
static inline void marshall_from_mar_ckpt_section_descriptor_t (
	SaCkptSectionDescriptorT *dest,
	mar_ckpt_section_descriptor_t *src)
{
	marshall_from_mar_ckpt_section_id_t (&dest->sectionId, &src->section_id);
	dest->expirationTime = src->expiration_time;
	dest->sectionSize = src->section_size;
	dest->sectionState = src->section_state;
	dest->lastUpdate = src->last_update;
}

static inline void marshall_to_mar_ckpt_section_descriptor_t (
	mar_ckpt_section_descriptor_t *dest,
	SaCkptSectionDescriptorT *src)
{
	memset (dest, 0, sizeof (mar_ckpt_section_descriptor_t));
	marshall_to_mar_ckpt_section_id_t (&dest->section_id, &src->sectionId);
	dest->expiration_time = src->expirationTime;
	dest->section_size = src->sectionSize;
	dest->section_state = src->sectionState;
	dest->last_update = src->lastUpdate;
}

typedef enum {
	MAR_CKPT_SECTIONS_FOREVER = 1,
	MAR_CKPT_SECTIONS_LEQ_EXPIRATION_TIME = 2,
	MAR_CKPT_SECTIONS_GEQ_EXPIRATION_TIME = 3,
	MAR_CKPT_SECTIONS_CORRUPTED = 4,
	MAR_CKPT_SECTIONS_ANY = 5
} mar_ckpt_sections_chosen_t;

typedef mar_uint64_t mar_offset_t;

static inline void swab_mar_offset_t (
	mar_offset_t *to_swab)
{
	swab_mar_uint64_t (to_swab);
}

/*
 * Marshalling the SaCkptIOVectorElementT (not needed)
 */
typedef struct {
	mar_ckpt_section_id_t section_id __attribute__((aligned(8)));
	void *data_buffer __attribute__((aligned(8)));
	mar_size_t data_size __attribute__((aligned(8)));
	mar_offset_t data_offset __attribute__((aligned(8)));
	mar_size_t read_size __attribute__((aligned(8)));
} mar_ckpt_io_vector_element_t;

/*
 * Marshalling the SaCkptCheckpointDescriptorT 
 */
typedef struct {
	mar_ckpt_checkpoint_creation_attributes_t checkpoint_creation_attributes __attribute__((aligned(8)));
	mar_uint32_t number_of_sections __attribute__((aligned(8)));
	mar_uint32_t memory_used __attribute__((aligned(8)));
} mar_ckpt_checkpoint_descriptor_t;

static inline void marshall_from_mar_ckpt_checkpoint_descriptor_t (
	SaCkptCheckpointDescriptorT *dest,
	mar_ckpt_checkpoint_descriptor_t *src)
{
	marshall_from_mar_ckpt_checkpoint_creation_attributes_t (
		&dest->checkpointCreationAttributes,
		&src->checkpoint_creation_attributes);
	dest->numberOfSections = src->number_of_sections;
	dest->memoryUsed = src->memory_used;
}

static inline void marshall_to_mar_ckpt_checkpoint_descriptor_t (
	mar_ckpt_checkpoint_descriptor_t *dest,
	SaCkptCheckpointDescriptorT *src)
{
	memset (dest, 0, sizeof (mar_ckpt_checkpoint_descriptor_t));
	marshall_to_mar_ckpt_checkpoint_creation_attributes_t (
		&dest->checkpoint_creation_attributes,
		&src->checkpointCreationAttributes);
	dest->number_of_sections = src->numberOfSections;
	dest->memory_used = src->memoryUsed;
}

#endif /* AIS_MAR_CKPT_H_DEFINED */
