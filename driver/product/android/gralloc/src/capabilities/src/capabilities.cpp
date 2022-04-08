/*
 * Copyright (C) 2020 ARM Limited. All rights reserved.
 *
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "capabilities.h"

#include <string.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <assert.h>
#include <pthread.h>

#include "core/format_info.h"

/* Writing to runtime_caps_read is guarded by mutex caps_init_mutex. */
static pthread_mutex_t caps_init_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool runtime_caps_read = false;

mali_gralloc_format_caps cpu_runtime_caps;
mali_gralloc_format_caps dpu_runtime_caps;
mali_gralloc_format_caps dpu_aeu_runtime_caps;
mali_gralloc_format_caps vpu_runtime_caps;
mali_gralloc_format_caps gpu_runtime_caps;
mali_gralloc_format_caps cam_runtime_caps;

#define MALI_GRALLOC_GPU_LIB_NAME "libGLES_mali.so"
#define MALI_GRALLOC_VPU_LIB_NAME "libstagefrighthw.so"
#define MALI_GRALLOC_DPU_LIB_NAME "hwcomposer.drm.so"
#define MALI_GRALLOC_DPU_AEU_LIB_NAME "dpu_aeu_fake_caps.so"
#define MALI_GRALLOC_VPU_LIBRARY_PATH "/vendor/lib/"

static bool get_block_capabilities(const char *name, mali_gralloc_format_caps *block_caps)
{
	void *dso_handle = NULL;
	bool rval = false;

	/* Clear any error conditions */
	dlerror();

	/* Look for MALI_GRALLOC_FORMATCAPS_SYM_NAME_STR symbol in user-space drivers
	 * to determine hw format capabilities.
	 */
	dso_handle = dlopen(name, RTLD_LAZY);

	if (dso_handle)
	{
		void *sym = dlsym(dso_handle, MALI_GRALLOC_FORMATCAPS_SYM_NAME_STR);

		if (sym)
		{
			memcpy((void *)block_caps, sym, sizeof(mali_gralloc_format_caps));
			rval = true;
		}
		dlclose(dso_handle);
	}
	else
	{
		MALI_GRALLOC_LOGW("Unable to dlopen %s shared object, error = %s", name, dlerror());
	}

	return rval;
}

void get_ip_capabilities(void)
{
	/* Ensure capability setting is not interrupted by other
	 * allocations during start-up.
	 */
	pthread_mutex_lock(&caps_init_mutex);

	if (runtime_caps_read)
	{
		goto already_init;
	}

	sanitize_formats();

	memset((void *)&cpu_runtime_caps, 0, sizeof(cpu_runtime_caps));
	memset((void *)&dpu_runtime_caps, 0, sizeof(dpu_runtime_caps));
	memset((void *)&dpu_aeu_runtime_caps, 0, sizeof(dpu_aeu_runtime_caps));
	memset((void *)&vpu_runtime_caps, 0, sizeof(vpu_runtime_caps));
	memset((void *)&gpu_runtime_caps, 0, sizeof(gpu_runtime_caps));
	memset((void *)&cam_runtime_caps, 0, sizeof(cam_runtime_caps));

	/* Determine CPU IP capabilities */
	cpu_runtime_caps.caps_mask |= MALI_GRALLOC_FORMAT_CAPABILITY_OPTIONS_PRESENT;
	cpu_runtime_caps.caps_mask |= MALI_GRALLOC_FORMAT_CAPABILITY_PIXFMT_RGBA1010102;
	cpu_runtime_caps.caps_mask |= MALI_GRALLOC_FORMAT_CAPABILITY_PIXFMT_RGBA16161616;

	/* Determine DPU IP capabilities */
	get_block_capabilities(MALI_GRALLOC_DPU_LIBRARY_PATH MALI_GRALLOC_DPU_LIB_NAME, &dpu_runtime_caps);

	/* Determine DPU AEU IP capabilities */
	get_block_capabilities(MALI_GRALLOC_DPU_AEU_LIBRARY_PATH MALI_GRALLOC_DPU_AEU_LIB_NAME, &dpu_aeu_runtime_caps);

	/* Determine GPU IP capabilities */
	if (access(MALI_GRALLOC_GPU_LIBRARY_PATH1 MALI_GRALLOC_GPU_LIB_NAME, R_OK) == 0)
	{
		get_block_capabilities(MALI_GRALLOC_GPU_LIBRARY_PATH1 MALI_GRALLOC_GPU_LIB_NAME, &gpu_runtime_caps);
	}
	else if (access(MALI_GRALLOC_GPU_LIBRARY_PATH2 MALI_GRALLOC_GPU_LIB_NAME, R_OK) == 0)
	{
		get_block_capabilities(MALI_GRALLOC_GPU_LIBRARY_PATH2 MALI_GRALLOC_GPU_LIB_NAME, &gpu_runtime_caps);
	}

	/* Determine VPU IP capabilities */
	get_block_capabilities(MALI_GRALLOC_VPU_LIBRARY_PATH MALI_GRALLOC_VPU_LIB_NAME, &vpu_runtime_caps);

/* Build specific capability changes */
#if defined(GRALLOC_ARM_NO_EXTERNAL_AFBC) && (GRALLOC_ARM_NO_EXTERNAL_AFBC == 1)
	{
		dpu_runtime_caps.caps_mask &= ~MALI_GRALLOC_FORMAT_CAPABILITY_AFBCENABLE_MASK;
		gpu_runtime_caps.caps_mask &= ~MALI_GRALLOC_FORMAT_CAPABILITY_AFBCENABLE_MASK;
		vpu_runtime_caps.caps_mask &= ~MALI_GRALLOC_FORMAT_CAPABILITY_AFBCENABLE_MASK;
		cam_runtime_caps.caps_mask &= ~MALI_GRALLOC_FORMAT_CAPABILITY_AFBCENABLE_MASK;
	}
#endif

#if defined(GRALLOC_CAMERA_WRITE_RAW16) && GRALLOC_CAMERA_WRITE_RAW16
		cam_runtime_caps.caps_mask |= MALI_GRALLOC_FORMAT_CAPABILITY_OPTIONS_PRESENT;
#endif

	runtime_caps_read = true;

already_init:
	pthread_mutex_unlock(&caps_init_mutex);

	MALI_GRALLOC_LOGV("GPU format capabilities 0x%" PRIx64, gpu_runtime_caps.caps_mask);
	MALI_GRALLOC_LOGV("DPU format capabilities 0x%" PRIx64, dpu_runtime_caps.caps_mask);
	MALI_GRALLOC_LOGV("VPU format capabilities 0x%" PRIx64, vpu_runtime_caps.caps_mask);
	MALI_GRALLOC_LOGV("CAM format capabilities 0x%" PRIx64, cam_runtime_caps.caps_mask);
}


/* This is used by the unit tests to get the capabilities for each IP. */
extern "C" {
	void mali_gralloc_get_caps(struct mali_gralloc_format_caps *gpu_caps,
	                           struct mali_gralloc_format_caps *vpu_caps,
	                           struct mali_gralloc_format_caps *dpu_caps,
	                           struct mali_gralloc_format_caps *dpu_aeu_caps,
	                           struct mali_gralloc_format_caps *cam_caps)
	{
		get_ip_capabilities();

		memcpy(gpu_caps, (void *)&gpu_runtime_caps, sizeof(*gpu_caps));
		memcpy(vpu_caps, (void *)&vpu_runtime_caps, sizeof(*vpu_caps));
		memcpy(dpu_caps, (void *)&dpu_runtime_caps, sizeof(*dpu_caps));
		memcpy(dpu_aeu_caps, (void *)&dpu_aeu_runtime_caps, sizeof(*dpu_aeu_caps));
		memcpy(cam_caps, (void *)&cam_runtime_caps, sizeof(*cam_caps));
	}
}
