/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "console.h"
#include "tpm_nvmem_read.h"

/* These come from the tpm2 tree. */
#include "Global.h"
#include "Implementation.h"
#include "NV_fp.h"
#include "tpm_types.h"

#define CPRINTF(format, args...) cprintf(CC_TASK, format, ## args)

enum tpm_read_rv read_tpm_nvmem(uint16_t obj_index,
				uint16_t obj_size, void *obj_value)
{
	TPMI_RH_NV_INDEX object_handle;
	NV_INDEX         nvIndex;

	object_handle = HR_NV_INDEX + obj_index;
	if (NvIndexIsAccessible(object_handle,
				TPM_CC_NV_Read) != TPM_RC_SUCCESS) {
		CPRINTF("%s: object at 0x%x not found\n", __func__, obj_index);
		return tpm_read_not_found;
	}

	/* Get properties of this index as stored in nvmem. */
	NvGetIndexInfo(object_handle, &nvIndex);

	/*
	 * We presume it is readable and are not checking the access
	 * limitations.
	 */

	/*
	 * Does the caller ask for too much? Note that we always read from the
	 * beginning of the space, unlike the actual TPM2_NV_Read command
	 * which can start at an offset.
	 */
	if (obj_size > nvIndex.publicArea.dataSize) {
		CPRINTF("%s: object at 0x%x is smaller than %d\n",
			__func__, obj_index, obj_size);
		return tpm_read_too_small;
	}

	/* Perform the read. */
	NvGetIndexData(object_handle, &nvIndex, 0, obj_size, obj_value);

	return tpm_read_success;
}
