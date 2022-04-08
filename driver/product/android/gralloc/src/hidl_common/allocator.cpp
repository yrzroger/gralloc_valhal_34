/*
 * Copyright (C) 2020-2021 ARM Limited. All rights reserved.
 *
 * Copyright 2016 The Android Open Source Project
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

/* Legacy shared attribute region is deprecated from Android 11.
 * Use the new shared metadata region defined for Gralloc 4.
 */
#define GRALLOC_USE_SHARED_METADATA (GRALLOC_VERSION_MAJOR > 3)

#include "allocator.h"
#if GRALLOC_USE_SHARED_METADATA
#include "shared_metadata.h"
#else
#include "gralloc/attributes.h"
#endif

#include "core/buffer_allocation.h"
#include "core/buffer_descriptor.h"
#include "core/format_info.h"
#include "allocator/allocator.h"
#include "allocator/shared_memory/shared_memory.h"
#include "gralloc_version.h"

namespace arm
{
namespace allocator
{
namespace common
{

void allocate(buffer_descriptor_t *bufferDescriptor, uint32_t count, IAllocator::allocate_cb hidl_cb)
{
	Error error = Error::NONE;
	int stride = 0;
	std::vector<hidl_handle> grallocBuffers;

	grallocBuffers.reserve(count);

	for (uint32_t i = 0; i < count; i++)
	{
		private_handle_t *hnd = nullptr;
		if (mali_gralloc_buffer_allocate(bufferDescriptor, &hnd) != 0)
		{
			MALI_GRALLOC_LOGE("%s, buffer allocation failed with %d", __func__, errno);
			error = Error::NO_RESOURCES;
			break;
		}

		hnd->imapper_version = HIDL_MAPPER_VERSION_SCALED;

#if GRALLOC_USE_SHARED_METADATA
		hnd->reserved_region_size = bufferDescriptor->reserved_size;
		hnd->attr_size = mapper::common::shared_metadata_size() + hnd->reserved_region_size;
#else
		hnd->attr_size = sizeof(attr_region);
#endif
		std::tie(hnd->share_attr_fd, hnd->attr_base) =
			gralloc_shared_memory_allocate("gralloc_shared_memory", hnd->attr_size);
		if (hnd->share_attr_fd < 0 || hnd->attr_base == MAP_FAILED)
		{
			MALI_GRALLOC_LOGE("%s, shared memory allocation failed with errno %d", __func__, errno);
			mali_gralloc_buffer_free(hnd);
			error = Error::UNSUPPORTED;
			break;
		}

#if GRALLOC_USE_SHARED_METADATA
		mapper::common::shared_metadata_init(hnd->attr_base, bufferDescriptor->name);
#else
		new(hnd->attr_base) attr_region;
#endif
		const uint32_t base_format = bufferDescriptor->alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK;
		const uint64_t usage = bufferDescriptor->consumer_usage | bufferDescriptor->producer_usage;
		android_dataspace_t dataspace;
		get_format_dataspace(base_format, usage, hnd->width, hnd->height, &dataspace, &hnd->yuv_info);

#if GRALLOC_USE_SHARED_METADATA
		mapper::common::set_dataspace(hnd, static_cast<mapper::common::Dataspace>(dataspace));
#else
		int temp_dataspace = static_cast<int>(dataspace);
		gralloc_buffer_attr_write(hnd, GRALLOC_ARM_BUFFER_ATTR_DATASPACE, &temp_dataspace);
#endif
		/*
		* We need to set attr_base to MAP_FAILED before the HIDL callback
		* to avoid sending an invalid pointer to the client process.
		*
		* hnd->attr_base = mmap(...);
		* hidl_callback(hnd); // client receives hnd->attr_base = <dangling pointer>
		*/
		munmap(hnd->attr_base, hnd->attr_size);
		hnd->attr_base = MAP_FAILED;

		int tmpStride = bufferDescriptor->pixel_stride;

		if (stride == 0)
		{
			stride = tmpStride;
		}
		else if (stride != tmpStride)
		{
			/* Stride must be the same for all allocations */
			mali_gralloc_buffer_free(hnd);
			stride = 0;
			error = Error::UNSUPPORTED;
			break;
		}

		grallocBuffers.emplace_back(hidl_handle(hnd));
	}

	/* Populate the array of buffers for application consumption */
	hidl_vec<hidl_handle> hidlBuffers;
	if (error == Error::NONE)
	{
		hidlBuffers.setToExternal(grallocBuffers.data(), grallocBuffers.size());
	}
	hidl_cb(error, stride, hidlBuffers);

	/* The application should import the Gralloc buffers using IMapper for
	 * further usage. Free the allocated buffers in IAllocator context
	 */
	for (auto &buffer : grallocBuffers)
	{
		const native_handle_t *native_handle = buffer.getNativeHandle();
		mali_gralloc_buffer_free(private_handle_t::downcast(native_handle));
		native_handle_delete(const_cast<native_handle_t *>(native_handle));
	}
}

} // namespace common
} // namespace allocator
} // namespace arm
