/*
 * Copyright (C) 2020-2021 Arm Limited.
 * SPDX-License-Identifier: Apache-2.0
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

#include <unordered_map>
#include "drm_utils.h"
#include "gralloc/formats.h"

enum class format_colormodel
{
	rgb,
	yuv,
};

struct table_entry
{
	uint32_t fourcc;
	format_colormodel colormodel;
};

const static std::unordered_map<uint64_t, table_entry> table =
{
	{ MALI_GRALLOC_FORMAT_INTERNAL_RAW16, {DRM_FORMAT_R16, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGBA_8888, {DRM_FORMAT_ABGR8888, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_BGRA_8888, {DRM_FORMAT_ARGB8888, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGB_565, {DRM_FORMAT_RGB565, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGBX_8888, {DRM_FORMAT_XBGR8888, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGB_888, {DRM_FORMAT_BGR888, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGBA_1010102, {DRM_FORMAT_ABGR2101010, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGBA_16161616, {DRM_FORMAT_ABGR16161616F, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_RGBA_10101010, {DRM_FORMAT_AXBXGXRX106106106106, format_colormodel::rgb} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YV12, {DRM_FORMAT_YVU420, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YU12, {DRM_FORMAT_YUV420, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_NV12, {DRM_FORMAT_NV12, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_NV15, {DRM_FORMAT_NV15, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_NV16, {DRM_FORMAT_NV16, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_NV21, {DRM_FORMAT_NV21, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_Y0L2, {DRM_FORMAT_Y0L2, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_Y210, {DRM_FORMAT_Y210, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_P010, {DRM_FORMAT_P010, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_P210, {DRM_FORMAT_P210, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_Y410, {DRM_FORMAT_Y410, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YUV444, {DRM_FORMAT_YUV444, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_Q410, {DRM_FORMAT_Q410, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_Q401, {DRM_FORMAT_Q401, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YUV422_8BIT, {DRM_FORMAT_YUYV, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YUV420_8BIT_I, {DRM_FORMAT_YUV420_8BIT, format_colormodel::yuv} },
	{ MALI_GRALLOC_FORMAT_INTERNAL_YUV420_10BIT_I, {DRM_FORMAT_YUV420_10BIT, format_colormodel::yuv} },

	/* Format introduced in Android P, mapped to MALI_GRALLOC_FORMAT_INTERNAL_P010. */
	{ HAL_PIXEL_FORMAT_YCBCR_P010, {DRM_FORMAT_P010, format_colormodel::yuv} },
};

uint32_t drm_fourcc_from_handle(const private_handle_t *hnd)
{
	/* Clean the modifier bits in the internal format. */
	const uint64_t unmasked_format = hnd->alloc_format;
	const uint64_t internal_format = get_internal_format_from_gralloc_format(unmasked_format);

	auto entry = table.find(internal_format);
	if (entry == table.end())
	{
		return DRM_FORMAT_INVALID;
	}

	bool afbc = is_format_afbc(unmasked_format);
	/* The internal RGB565 format describes two different component orderings depending on AFBC. */
	if (afbc && internal_format == MALI_GRALLOC_FORMAT_INTERNAL_RGB_565)
	{
		return DRM_FORMAT_BGR565;
	}

	return entry->second.fourcc;
}

static uint64_t get_afrc_modifier_tags(const private_handle_t *hnd)
{
	const uint64_t unmasked_format = hnd->alloc_format;
	if (!is_format_afrc(unmasked_format))
	{
		return 0;
	}

	const uint64_t internal_format = get_internal_format_from_gralloc_format(unmasked_format);
	const uint64_t internal_modifier = get_modifier_from_gralloc_format(unmasked_format);
	uint64_t modifier = 0;

	auto entry = table.find(internal_format);
	if (entry == table.end())
	{
		return 0;
	}

	if (!(internal_modifier & MALI_GRALLOC_INTFMT_AFRC_ROT_LAYOUT))
	{
		modifier |= AFRC_FORMAT_MOD_LAYOUT_SCAN;
	}

	/* If the afrc format is in yuv colormodel it should also have more than a single plane */
	if (entry->second.colormodel == format_colormodel::yuv && hnd->is_multi_plane())
	{
		const uint64_t luma_block_size = internal_modifier &
			MALI_GRALLOC_INTFMT_AFRC_LUMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_MASK);
		const uint64_t chroma_block_size = internal_modifier &
			MALI_GRALLOC_INTFMT_AFRC_CHROMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_MASK);

		switch (luma_block_size)
		{
		case MALI_GRALLOC_INTFMT_AFRC_LUMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_32):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_LUMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_24):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_LUMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_16):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16);
			break;
		}

		switch (chroma_block_size)
		{
		case MALI_GRALLOC_INTFMT_AFRC_CHROMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_32):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P12(AFRC_FORMAT_MOD_CU_SIZE_32);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_CHROMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_24):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P12(AFRC_FORMAT_MOD_CU_SIZE_24);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_CHROMA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_16):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P12(AFRC_FORMAT_MOD_CU_SIZE_16);
			break;
		}
	} else
	{
		const uint64_t rgba_block_size = internal_modifier &
			MALI_GRALLOC_INTFMT_AFRC_RGBA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_MASK);

		switch (rgba_block_size)
		{
		case MALI_GRALLOC_INTFMT_AFRC_RGBA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_32):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_32);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_RGBA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_24):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_24);
			break;
		case MALI_GRALLOC_INTFMT_AFRC_RGBA_CODING_UNIT_BYTES(MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_16):
			modifier |= AFRC_FORMAT_MOD_CU_SIZE_P0(AFRC_FORMAT_MOD_CU_SIZE_16);
			break;
		}
	}

	return DRM_FORMAT_MOD_ARM_AFRC(modifier);
}

static uint64_t get_afbc_modifier_tags(const private_handle_t *hnd)
{
	const uint64_t internal_format = hnd->alloc_format;
	if (!is_format_afbc(internal_format))
	{
		return 0;
	}

	uint64_t modifier = 0;

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_SPLITBLK)
	{
		modifier |= AFBC_FORMAT_MOD_SPLIT;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS)
	{
		modifier |= AFBC_FORMAT_MOD_TILED;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_DOUBLE_BODY)
	{
		modifier |= AFBC_FORMAT_MOD_DB;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_BCH)
	{
		modifier |= AFBC_FORMAT_MOD_BCH;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_YUV_TRANSFORM)
	{
		modifier |= AFBC_FORMAT_MOD_YTR;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_SPARSE)
	{
		modifier |= AFBC_FORMAT_MOD_SPARSE;
	}

	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_USM)
	{
		modifier |= AFBC_FORMAT_MOD_USM;
	}

	/* Extract the block-size modifiers. */
	if (internal_format & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK)
	{
		modifier |= (hnd->is_multi_plane() ? AFBC_FORMAT_MOD_BLOCK_SIZE_32x8_64x4 : AFBC_FORMAT_MOD_BLOCK_SIZE_32x8);
	}
	else if (internal_format & MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK)
	{
		modifier |= AFBC_FORMAT_MOD_BLOCK_SIZE_64x4;
	}
	else
	{
		modifier |= AFBC_FORMAT_MOD_BLOCK_SIZE_16x16;
	}

	return DRM_FORMAT_MOD_ARM_AFBC(modifier);
}

uint64_t drm_modifier_from_handle(const private_handle_t *hnd)
{
	if (is_format_afbc(hnd->alloc_format))
	{
		return get_afbc_modifier_tags(hnd);
	}
	else if (is_format_afrc(hnd->alloc_format))
	{
		return get_afrc_modifier_tags(hnd);
	}
	else if (is_format_block_linear(hnd->alloc_format))
	{
		return DRM_FORMAT_MOD_GENERIC_16_16_TILE;
	}
	return 0;
}
