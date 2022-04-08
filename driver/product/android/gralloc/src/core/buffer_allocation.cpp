/*
 * Copyright (C) 2016-2021 ARM Limited. All rights reserved.
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

#include <inttypes.h>
#include <assert.h>
#include <atomic>
#include <algorithm>

#include <hardware/hardware.h>
#include <hardware/gralloc1.h>

#include "buffer_allocation.h"
#include "allocator/allocator.h"
#include "allocator/shared_memory/shared_memory.h"
#include "private_interface_types.h"
#include "buffer.h"
#include "gralloc/attributes.h"
#include "buffer_descriptor.h"
#include "log.h"
#include "format_info.h"
#include "usages.h"

#define AFBC_PIXELS_PER_BLOCK 256
#define AFBC_HEADER_BUFFER_BYTES_PER_BLOCKENTRY 16

bool afbc_format_fallback(uint32_t * const format_idx, const uint64_t usage, bool force);


/*
 * Get a global unique ID
 */
static uint64_t getUniqueId()
{
	static std::atomic<uint32_t> counter(0);
	uint64_t id = static_cast<uint64_t>(getpid()) << 32;
	return id | counter++;
}

static void afbc_buffer_align(const bool is_tiled, int *size)
{
	const uint16_t AFBC_BODY_BUFFER_BYTE_ALIGNMENT = 1024;

	int buffer_byte_alignment = AFBC_BODY_BUFFER_BYTE_ALIGNMENT;

	if (is_tiled)
	{
		buffer_byte_alignment = 4 * AFBC_BODY_BUFFER_BYTE_ALIGNMENT;
	}

	*size = GRALLOC_ALIGN(*size, buffer_byte_alignment);
}

static uint32_t afrc_plane_alignment_requirement(uint32_t coding_unit_size)
{
	switch (coding_unit_size)
	{
	case 16:
		return 1024;
	case 24:
		return 512;
	case 32:
		return 2048;
	default:
		MALI_GRALLOC_LOGE("internal error: invalid coding unit size (%" PRIu32 ")", coding_unit_size);
		return 0;
	}
}

/*
 * Obtain AFBC superblock dimensions from type.
 */
static rect_t get_afbc_sb_size(AllocBaseType alloc_base_type)
{
	const uint16_t AFBC_BASIC_BLOCK_WIDTH = 16;
	const uint16_t AFBC_BASIC_BLOCK_HEIGHT = 16;
	const uint16_t AFBC_WIDE_BLOCK_WIDTH = 32;
	const uint16_t AFBC_WIDE_BLOCK_HEIGHT = 8;
	const uint16_t AFBC_EXTRAWIDE_BLOCK_WIDTH = 64;
	const uint16_t AFBC_EXTRAWIDE_BLOCK_HEIGHT = 4;

	rect_t sb = {0, 0};

	switch(alloc_base_type)
	{
		case AllocBaseType::AFBC:
			sb.width = AFBC_BASIC_BLOCK_WIDTH;
			sb.height = AFBC_BASIC_BLOCK_HEIGHT;
			break;
		case AllocBaseType::AFBC_WIDEBLK:
			sb.width = AFBC_WIDE_BLOCK_WIDTH;
			sb.height = AFBC_WIDE_BLOCK_HEIGHT;
			break;
		case AllocBaseType::AFBC_EXTRAWIDEBLK:
			sb.width = AFBC_EXTRAWIDE_BLOCK_WIDTH;
			sb.height = AFBC_EXTRAWIDE_BLOCK_HEIGHT;
			break;
		default:
			break;
	}
	return sb;
}

/*
 * Obtain AFBC superblock dimensions for specific plane.
 *
 * See alloc_type_t for more information.
 */
static rect_t get_afbc_sb_size(alloc_type_t alloc_type, const uint8_t plane)
{
	if (plane > 0 && alloc_type.is_afbc() && alloc_type.is_multi_plane)
	{
		return get_afbc_sb_size(AllocBaseType::AFBC_EXTRAWIDEBLK);
	}
	else
	{
		return get_afbc_sb_size(alloc_type.primary_type);
	}
}

bool get_alloc_type(const uint64_t format_ext,
                    const uint32_t format_idx,
                    const uint64_t usage,
                    alloc_type_t * const alloc_type)
{
	alloc_type->primary_type = AllocBaseType::UNCOMPRESSED;
	alloc_type->is_multi_plane = formats[format_idx].npln > 1;
	alloc_type->is_tiled = false;
	alloc_type->is_padded = false;
	alloc_type->is_frontbuffer_safe = false;

	/* Determine AFBC type for this format. This is used to decide alignment.
	   Split block does not affect alignment, and therefore doesn't affect the allocation type. */
	if (is_format_afbc(format_ext))
	{
		/* YUV transform shall not be enabled for a YUV format */
		if ((formats[format_idx].is_yuv == true) && (format_ext & MALI_GRALLOC_INTFMT_AFBC_YUV_TRANSFORM))
		{
			MALI_GRALLOC_LOGW("YUV Transform is incorrectly enabled for format = 0x%x. Extended internal format = 0x%" PRIx64 "\n",
			       formats[format_idx].id, format_ext);
		}

		/* Determine primary AFBC (superblock) type. */
		alloc_type->primary_type = AllocBaseType::AFBC;
		if (format_ext & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK)
		{
			alloc_type->primary_type = AllocBaseType::AFBC_WIDEBLK;
		}
		else if (format_ext & MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK)
		{
			alloc_type->primary_type = AllocBaseType::AFBC_EXTRAWIDEBLK;
		}

		if (format_ext & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS)
		{
			alloc_type->is_tiled = true;

			if (formats[format_idx].npln > 1 &&
				(format_ext & MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK) == 0)
			{
				MALI_GRALLOC_LOGW("Extra-wide AFBC must be signalled for multi-plane formats. "
				      "Falling back to single plane AFBC.");
				alloc_type->is_multi_plane = false;
			}

			if (format_ext & MALI_GRALLOC_INTFMT_AFBC_DOUBLE_BODY)
			{
				alloc_type->is_frontbuffer_safe = true;
			}
		}
		else
		{
			if (formats[format_idx].npln > 1)
			{
				MALI_GRALLOC_LOGW("Multi-plane AFBC is not supported without tiling. "
				      "Falling back to single plane AFBC.");
			}
			alloc_type->is_multi_plane = false;
		}

		if (format_ext & MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK &&
			!alloc_type->is_tiled)
		{
			/* Headers must be tiled for extra-wide. */
			MALI_GRALLOC_LOGE("ERROR: Invalid to specify extra-wide block without tiled headers.");
			return false;
		}

		if (alloc_type->is_frontbuffer_safe &&
		    (format_ext & (MALI_GRALLOC_INTFMT_AFBC_WIDEBLK | MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK)))
		{
			MALI_GRALLOC_LOGE("ERROR: Front-buffer safe not supported with wide/extra-wide block.");
		}

		if (formats[format_idx].npln == 1 &&
		    format_ext & MALI_GRALLOC_INTFMT_AFBC_WIDEBLK &&
		    format_ext & MALI_GRALLOC_INTFMT_AFBC_EXTRAWIDEBLK)
		{
			/* "Wide + Extra-wide" implicitly means "multi-plane". */
			MALI_GRALLOC_LOGE("ERROR: Invalid to specify multiplane AFBC with single plane format.");
			return false;
		}

		if (usage & MALI_GRALLOC_USAGE_AFBC_PADDING)
		{
			alloc_type->is_padded = true;
		}
	}
	else if (is_format_afrc(format_ext))
	{
		const format_info_t &format = formats[format_idx];

		alloc_type->primary_type = AllocBaseType::AFRC;

		if (format_ext & MALI_GRALLOC_INTFMT_AFRC_ROT_LAYOUT)
		{
			alloc_type->afrc.paging_tile_width = 8;
			alloc_type->afrc.paging_tile_height = 8;
		}
		else
		{
			alloc_type->afrc.paging_tile_width = 16;
			alloc_type->afrc.paging_tile_height = 4;
		}

		alloc_type->afrc.rgba_luma_coding_unit_bytes = MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_UNWRAP(
		    (format_ext >> MALI_GRALLOC_INTFMT_AFRC_RGBA_CODING_UNIT_BYTES_SHIFT) &
		    MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_MASK);
		alloc_type->afrc.rgba_luma_plane_alignment = afrc_plane_alignment_requirement(
		    alloc_type->afrc.rgba_luma_coding_unit_bytes);
		if (alloc_type->afrc.rgba_luma_plane_alignment == 0)
		{
			return false;
		}

		alloc_type->afrc.chroma_coding_unit_bytes = MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_UNWRAP(
		    (format_ext >> MALI_GRALLOC_INTFMT_AFRC_CHROMA_CODING_UNIT_BYTES_SHIFT) &
		    MALI_GRALLOC_INTFMT_AFRC_CODING_UNIT_BYTES_MASK);
		alloc_type->afrc.chroma_plane_alignment = afrc_plane_alignment_requirement(
		    alloc_type->afrc.chroma_coding_unit_bytes);
		if (alloc_type->afrc.chroma_plane_alignment == 0)
		{
			return false;
		}

		for (auto plane = 0; plane < format.npln; ++plane)
		{
			switch (format.ncmp[plane])
			{
			case 1:
				alloc_type->afrc.clump_width[plane] = alloc_type->afrc.paging_tile_width;
				alloc_type->afrc.clump_height[plane] = alloc_type->afrc.paging_tile_height;
				break;
			case 2:
				alloc_type->afrc.clump_width[plane] = 8;
				alloc_type->afrc.clump_height[plane] = 4;
				break;
			case 3:
			case 4:
				alloc_type->afrc.clump_width[plane] = 4;
				alloc_type->afrc.clump_height[plane] = 4;
				break;
			default:
				MALI_GRALLOC_LOGE("internal error: invalid number of components in plane %d (%d)",
				                  static_cast<int>(plane), static_cast<int>(format.ncmp[plane]));
				return false;
			}
		}
	}
	else if (is_format_block_linear(format_ext))
	{
		alloc_type->primary_type = AllocBaseType::BLOCK_LINEAR;
	}
	return true;
}

/*
 * Initialise AFBC header based on superblock layout.
 * Width and height should already be AFBC aligned.
 */
void init_afbc(uint8_t *buf, const uint64_t alloc_format,
               const bool is_multi_plane,
               const int w, const int h)
{
	const bool is_tiled = ((alloc_format & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS)
	                         == MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS);
	const uint32_t n_headers = (w * h) / AFBC_PIXELS_PER_BLOCK;
	int body_offset = n_headers * AFBC_HEADER_BUFFER_BYTES_PER_BLOCKENTRY;

	afbc_buffer_align(is_tiled, &body_offset);

	/*
	 * Declare the AFBC header initialisation values for each superblock layout.
	 * Tiled headers (AFBC 1.2) can be initialised to zero for non-subsampled formats
	 * (SB layouts: 0, 3, 4, 7).
	 */
	uint32_t headers[][4] = {
		{ (uint32_t)body_offset, 0x1, 0x10000, 0x0 }, /* Layouts 0, 3, 4, 7 */
		{ ((uint32_t)body_offset + (1 << 28)), 0x80200040, 0x1004000, 0x20080 } /* Layouts 1, 5 */
	};
	if ((alloc_format & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS))
	{
		/* Zero out body_offset for non-subsampled formats. */
		memset(headers[0], 0, sizeof(uint32_t) * 4);
	}

	/* Map base format to AFBC header layout */
	const uint32_t base_format = alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK;

	/* Sub-sampled formats use layouts 1 and 5 which is index 1 in the headers array.
	 * 1 = 4:2:0 16x16, 5 = 4:2:0 32x8.
	 *
	 * Non-subsampled use layouts 0, 3, 4 and 7, which is index 0.
	 * 0 = 16x16, 3 = 32x8 + split, 4 = 32x8, 7 = 64x4.
	 *
	 * When using separated planes for YUV formats, the header layout is the non-subsampled one
	 * as there is a header per-plane and there is no sub-sampling within the plane.
	 * Separated plane only supports 32x8 or 64x4 for the luma plane, so the first plane must be 4 or 7.
	 * Seperated plane only supports 64x4 for subsequent planes, so these must be header layout 7.
	 */
	const uint32_t layout = is_subsampled_yuv(base_format) && !is_multi_plane ? 1 : 0;

	MALI_GRALLOC_LOGV("Writing AFBC header layout %d for format %" PRIx32, layout, base_format);

	for (uint32_t i = 0; i < n_headers; i++)
	{
		memcpy(buf, headers[layout], sizeof(headers[layout]));
		buf += sizeof(headers[layout]);
	}
}

static int max(int a, int b)
{
	return a > b ? a : b;
}

static int max(int a, int b, int c)
{
	return c > max(a, b) ? c : max(a, b);
}

static int max(int a, int b, int c, int d)
{
	return d > max(a, b, c) ? d : max(a, b, c);
}

/*
 * Obtain plane allocation dimensions (in pixels).
 *
 * NOTE: pixel stride, where defined for format, is
 * incorporated into allocation dimensions.
 */
static void get_pixel_w_h(uint32_t * const width,
                          uint32_t * const height,
                          const format_info_t format,
                          const alloc_type_t alloc_type,
                          const uint8_t plane,
                          bool has_cpu_usage)
{
	const rect_t sb = get_afbc_sb_size(alloc_type, plane);

	/*
	 * Round-up plane dimensions, to multiple of:
	 * - Samples for all channels (sub-sampled formats)
	 * - Memory bytes/words (some packed formats)
	 */
	*width = GRALLOC_ALIGN(*width, format.align_w);
	*height = GRALLOC_ALIGN(*height, format.align_h);

	/*
	 * Sub-sample (sub-sampled) planes.
	 */
	if (plane > 0)
	{
		*width /= format.hsub;
		*height /= format.vsub;
	}

	/*
	 * Pixel alignment (width),
	 * where format stride is stated in pixels.
	 */
	int pixel_align_w = 1, pixel_align_h = 1;
	if (has_cpu_usage)
	{
		pixel_align_w = format.align_w_cpu;
	}
	else if (alloc_type.is_afbc())
	{
#define HEADER_STRIDE_ALIGN_IN_SUPER_BLOCKS (0)
		uint32_t num_sb_align = 0;
		if (alloc_type.is_padded && !format.is_yuv)
		{
			/* Align to 4 superblocks in width --> 64-byte,
			 * assuming 16-byte header per superblock.
			 */
			num_sb_align = 4;
		}
		pixel_align_w = max(HEADER_STRIDE_ALIGN_IN_SUPER_BLOCKS, num_sb_align) * sb.width;

		/*
		 * Determine AFBC tile size when allocating tiled headers.
		 */
		rect_t afbc_tile = sb;
		if (alloc_type.is_tiled)
		{
			afbc_tile.width = format.bpp_afbc[plane] > 32 ? 4 * afbc_tile.width : 8 * afbc_tile.width;
			afbc_tile.height = format.bpp_afbc[plane] > 32 ? 4 * afbc_tile.height : 8 * afbc_tile.height;
		}

		MALI_GRALLOC_LOGV("Plane[%hhu]: [SUB-SAMPLE] w:%d, h:%d\n", plane, *width, *height);
		MALI_GRALLOC_LOGV("Plane[%hhu]: [PIXEL_ALIGN] w:%d\n", plane, pixel_align_w);
		MALI_GRALLOC_LOGV("Plane[%hhu]: [LINEAR_TILE] w:%" PRIu16 "\n", plane, format.tile_size);
		MALI_GRALLOC_LOGV("Plane[%hhu]: [AFBC_TILE] w:%" PRIu16 ", h:%" PRIu16 "\n", plane, afbc_tile.width, afbc_tile.height);

		pixel_align_w = max(pixel_align_w, afbc_tile.width);
		pixel_align_h = max(pixel_align_h, afbc_tile.height);

		if (AllocBaseType::AFBC_WIDEBLK == alloc_type.primary_type && !alloc_type.is_tiled)
		{
			/*
			 * Special case for wide block (32x8) AFBC with linear (non-tiled)
			 * headers: hardware reads and writes 32x16 blocks so we need to
			 * pad the body buffer accordingly.
			 *
			 * Note that this branch will not be taken for multi-plane AFBC
			 * since that requires tiled headers.
			 */
			pixel_align_h = max(pixel_align_h, 16);
		}
	}
	else if (alloc_type.is_afrc())
	{
		pixel_align_w = alloc_type.afrc.paging_tile_width * alloc_type.afrc.clump_width[plane];
		pixel_align_h = alloc_type.afrc.paging_tile_height * alloc_type.afrc.clump_height[plane];
	}
	else if (alloc_type.is_block_linear())
	{
		pixel_align_w = pixel_align_h = 16;
	}
	*width = GRALLOC_ALIGN(*width, max(1, pixel_align_w, format.tile_size));
	*height = GRALLOC_ALIGN(*height, max(1, pixel_align_h, format.tile_size));
}



static uint32_t gcd(uint32_t a, uint32_t b)
{
	uint32_t r, t;

	if (a == b)
	{
		return a;
	}
	else if (a < b)
	{
		t = a;
		a = b;
		b = t;
	}

	while (b != 0)
	{
		r = a % b;
		a = b;
		b = r;
	}

	return a;
}

uint32_t lcm(uint32_t a, uint32_t b)
{
	if (a != 0 && b != 0)
	{
		return (a * b) / gcd(a, b);
	}

	return max(a, b);
}


/*
 * YV12 stride has additional complexity since chroma stride
 * must conform to the following:
 *
 * c_stride = ALIGN(stride/2, 16)
 *
 * Since the stride alignment must satisfy both CPU and HW
 * constraints, the luma stride must be doubled.
 */
static void update_yv12_stride(int8_t plane,
                               uint32_t luma_stride,
                               uint32_t stride_align,
                               uint32_t * byte_stride)
{
	if (plane == 0)
	{
		/*
		 * Ensure luma stride is aligned to "2*lcm(hw_align, cpu_align)" so
		 * that chroma stride can satisfy both CPU and HW alignment
		 * constraints when only half luma stride (as mandated for format).
		 */
		*byte_stride = GRALLOC_ALIGN(luma_stride, 2 * stride_align);
	}
	else
	{
		/*
		 * Derive chroma stride from luma and verify it is:
		 * 1. Aligned to lcm(hw_align, cpu_align)
		 * 2. Multiple of 16px (16 bytes)
		 */
		*byte_stride = luma_stride / 2;
		assert(*byte_stride == GRALLOC_ALIGN(*byte_stride, stride_align));
		assert((*byte_stride & 15) == 0);
	}
}



/*
 * Calculate allocation size.
 *
 * Determine the width and height of each plane based on pixel alignment for
 * both uncompressed and AFBC allocations.
 *
 * @param width           [in]    Buffer width.
 * @param height          [in]    Buffer height.
 * @param alloc_type      [in]    Allocation type inc. whether tiled and/or multi-plane.
 * @param format          [in]    Pixel format.
 * @param has_cpu_usage   [in]    CPU usage requested (in addition to any other).
 * @param pixel_stride    [out]   Calculated pixel stride.
 * @param size            [out]   Total calculated buffer size including all planes.
 * @param plane_info      [out]   Array of calculated information for each plane. Includes
 *                                offset, byte stride and allocation width and height.
 */
static void calc_allocation_size(const int width,
                                 const int height,
                                 const alloc_type_t alloc_type,
                                 const format_info_t format,
                                 const bool has_cpu_usage,
                                 const bool has_hw_usage,
                                 int * const pixel_stride,
                                 size_t * const size,
                                 plane_info_t plane_info[MAX_PLANES])
{
	plane_info[0].offset = 0;

	*size = 0;
	for (uint8_t plane = 0; plane < format.npln; plane++)
	{
		plane_info[plane].alloc_width = width;
		plane_info[plane].alloc_height = height;
		get_pixel_w_h(&plane_info[plane].alloc_width,
		              &plane_info[plane].alloc_height,
		              format,
		              alloc_type,
		              plane,
		              has_cpu_usage);
		MALI_GRALLOC_LOGV("Aligned w=%d, h=%d (in pixels)",
		      plane_info[plane].alloc_width, plane_info[plane].alloc_height);

		/*
		 * Calculate byte stride (per plane).
		 */
		if (alloc_type.is_afrc())
		{
			uint32_t coding_unit_bytes = plane == 0
			    ? alloc_type.afrc.rgba_luma_coding_unit_bytes
			    : alloc_type.afrc.chroma_coding_unit_bytes;

			uint32_t paging_tile_stride =
			    plane_info[plane].alloc_width / alloc_type.afrc.clump_width[plane] / alloc_type.afrc.paging_tile_width;
			const uint32_t coding_units_in_paging_tile = 64;
			plane_info[plane].byte_stride = paging_tile_stride * coding_units_in_paging_tile * coding_unit_bytes;
		}
		else if (alloc_type.is_afbc())
		{
			assert((plane_info[plane].alloc_width * format.bpp_afbc[plane]) % 8 == 0);
			plane_info[plane].byte_stride = (plane_info[plane].alloc_width * format.bpp_afbc[plane]) / 8;
		}
		else if (alloc_type.is_block_linear())
		{
			assert((plane_info[plane].alloc_width * format.bpp[plane]) % 8 == 0);
			uint32_t sample_height = 16;
			uint32_t sample_width = 16;
			if (plane > 0)
			{
				sample_height /= format.vsub;
				sample_width /= format.hsub;
			}
			uint32_t bytes_per_block = sample_height * sample_width * format.bpp[plane] / 8;
			uint32_t number_of_x_blocks = plane_info[0].alloc_width / 16;

			/* stride becomes equal to a row of blocks */
			plane_info[plane].byte_stride = number_of_x_blocks * bytes_per_block;
		}
		else
		{
			assert((plane_info[plane].alloc_width * format.bpp[plane]) % 8 == 0);
			plane_info[plane].byte_stride = (plane_info[plane].alloc_width * format.bpp[plane]) / 8;

			/*
			 * Align byte stride (uncompressed allocations only).
			 *
			 * Find the lowest-common-multiple of:
			 * 1. hw_align: Minimum byte stride alignment for HW IP (has_hw_usage == true)
			 * 2. cpu_align: Byte equivalent of 'align_w_cpu' (has_cpu_usage == true)
			 *
			 * NOTE: Pixel stride is defined as multiple of 'align_w_cpu'.
			 */
			uint16_t hw_align = 0;
			if (has_hw_usage)
			{
				hw_align = format.is_yuv ? 128 : 64;
			}

			uint32_t cpu_align = 0;
			if (has_cpu_usage)
			{
				assert((format.bpp[plane] * format.align_w_cpu) % 8 == 0);
				cpu_align = (format.bpp[plane] * format.align_w_cpu) / 8;
			}

			uint32_t stride_align = lcm(hw_align, cpu_align);
			if (stride_align)
			{
				plane_info[plane].byte_stride = GRALLOC_ALIGN(plane_info[plane].byte_stride * format.tile_size, stride_align) / format.tile_size;
			}

			/*
			 * Update YV12 stride with both CPU & HW usage due to constraint of chroma stride.
			 * Width is anyway aligned to 16px for luma and chroma (has_cpu_usage).
			 */
			if (format.id == MALI_GRALLOC_FORMAT_INTERNAL_YV12 && has_hw_usage && has_cpu_usage)
			{
				update_yv12_stride(plane,
				                   plane_info[0].byte_stride,
				                   stride_align,
				                   &plane_info[plane].byte_stride);
			}
		}
		MALI_GRALLOC_LOGV("Byte stride: %d", plane_info[plane].byte_stride);

		/*
		 * Pixel stride (CPU usage only).
		 * Not used in size calculation but exposed to client.
		 */
		if (plane == 0)
		{
			*pixel_stride = 0;
			const bool is_cpu_accessible =
			    !alloc_type.is_afbc() && !alloc_type.is_afrc() && !alloc_type.is_block_linear() && has_cpu_usage;
			if (is_cpu_accessible)
			{
				assert((plane_info[plane].byte_stride * 8) % format.bpp[plane] == 0);
				*pixel_stride = (plane_info[plane].byte_stride * 8) / format.bpp[plane];
			}

			MALI_GRALLOC_LOGV("Pixel stride: %d", *pixel_stride);
		}

		const uint32_t sb_num = (plane_info[plane].alloc_width * plane_info[plane].alloc_height)
		                      / AFBC_PIXELS_PER_BLOCK;

		/*
		 * Calculate body size (per plane).
		 */
		int body_size = 0;
		if (alloc_type.is_afbc())
		{
			const rect_t sb = get_afbc_sb_size(alloc_type, plane);
			const int sb_bytes = GRALLOC_ALIGN((format.bpp_afbc[plane] * sb.width * sb.height) / 8, 128);
			body_size = sb_num * sb_bytes;

			/* When AFBC planes are stored in separate buffers and this is not the last plane,
			   also align the body buffer to make the subsequent header aligned. */
			if (format.npln > 1 && plane < 2)
			{
				afbc_buffer_align(alloc_type.is_tiled, &body_size);
			}

			if (alloc_type.is_frontbuffer_safe)
			{
				int back_buffer_size = body_size;
				afbc_buffer_align(alloc_type.is_tiled, &back_buffer_size);
				body_size += back_buffer_size;
			}
		}
		else if (alloc_type.is_afrc())
		{
			uint32_t alignment = plane == 0
			    ? alloc_type.afrc.rgba_luma_plane_alignment
			    : alloc_type.afrc.chroma_plane_alignment;
			*size = GRALLOC_ALIGN(*size, alignment);

			uint32_t coding_unit_bytes = plane == 0
			    ? alloc_type.afrc.rgba_luma_coding_unit_bytes
			    : alloc_type.afrc.chroma_coding_unit_bytes;
			uint32_t s_coding_units = plane_info[plane].alloc_width / alloc_type.afrc.clump_width[plane];
			uint32_t t_coding_units = plane_info[plane].alloc_height / alloc_type.afrc.clump_height[plane];
			body_size = s_coding_units * t_coding_units * coding_unit_bytes;
		}
		else if (alloc_type.is_block_linear())
		{
			uint32_t number_of_blocks_y = plane_info[0].alloc_height / 16;
			body_size = plane_info[plane].byte_stride * number_of_blocks_y;
		}
		else
		{
			body_size = plane_info[plane].byte_stride * plane_info[plane].alloc_height;
		}
		MALI_GRALLOC_LOGV("Body size: %d", body_size);


		/*
		 * Calculate header size (per plane).
		 */
		int header_size = 0;
		if (alloc_type.is_afbc())
		{
			/* As this is AFBC, calculate header size for this plane.
			 * Always align the header, which will make the body buffer aligned.
			 */
			header_size = sb_num * AFBC_HEADER_BUFFER_BYTES_PER_BLOCKENTRY;
			afbc_buffer_align(alloc_type.is_tiled, &header_size);
		}
		MALI_GRALLOC_LOGV("AFBC Header size: %d", header_size);

		/*
		 * Set offset for separate chroma planes.
		 */
		if (plane > 0)
		{
			plane_info[plane].offset = *size;
		}

		/*
		 * Set overall size.
		 * Size must be updated after offset.
		 */
		*size += body_size + header_size;
		MALI_GRALLOC_LOGV("size=%zu",*size);
	}
}



/*
 * Validate selected format against requested.
 * Return true if valid, false otherwise.
 */
static bool validate_format(const format_info_t * const format,
                            const alloc_type_t alloc_type,
                            const buffer_descriptor_t * const bufDescriptor)
{
	if (alloc_type.is_afbc())
	{
		/*
		 * Validate format is supported by AFBC specification and gralloc.
		 */
		if (format->afbc == false)
		{
			MALI_GRALLOC_LOGE("ERROR: AFBC selected but not supported for base format: 0x%" PRIx32, format->id);
			return false;
		}

		/*
		 * Enforce consistency between number of format planes and
		 * request for single/multi-plane AFBC.
		 */
		if (((format->npln == 1 && alloc_type.is_multi_plane) ||
		    (format->npln > 1 && !alloc_type.is_multi_plane)))
		{
			MALI_GRALLOC_LOGE("ERROR: Format (%" PRIx32 ", num planes: %u) is incompatible with %s-plane AFBC request",
			      format->id, format->npln, (alloc_type.is_multi_plane) ? "multi" : "single");
			return false;
		}
	}
	else if(alloc_type.is_afrc())
	{
		if (!format->afrc)
		{
			MALI_GRALLOC_LOGE("ERROR: AFRC format requested but not supported for base format: %" PRIx32, format->id);
			return false;
		}
	}
	else if (alloc_type.is_block_linear())
	{
		if (!format->block_linear)
		{
			MALI_GRALLOC_LOGE("ERROR: Block Linear format requested but not supported for base format: %" PRIx32, format->id);
			return false;
		}
	}
	else
	{
		if (format->linear == false)
		{
			MALI_GRALLOC_LOGE("ERROR: Uncompressed format requested but not supported for base format: %" PRIx32, format->id);
			return false;
		}
	}

	if (format->id == MALI_GRALLOC_FORMAT_INTERNAL_BLOB &&
	    bufDescriptor->height != 1)
	{
		MALI_GRALLOC_LOGE("ERROR: Height for format BLOB must be 1.");
		return false;
	}

	return true;
}

int mali_gralloc_derive_format_and_size(buffer_descriptor_t *descriptor)
{
	alloc_type_t alloc_type{};
	int err;

	int alloc_width = descriptor->width;
	int alloc_height = descriptor->height;
	uint64_t usage = descriptor->producer_usage | descriptor->consumer_usage;

	/*
	* Select optimal internal pixel format based upon
	* usage and requested format.
	*/
	descriptor->alloc_format = mali_gralloc_select_format(descriptor->hal_format,
	                                                         descriptor->format_type,
	                                                         usage);
	if (descriptor->alloc_format == MALI_GRALLOC_FORMAT_INTERNAL_UNDEFINED)
	{
		MALI_GRALLOC_LOGE("ERROR: Unrecognized and/or unsupported format 0x%" PRIx64 " and usage 0x%" PRIx64,
		       descriptor->hal_format, usage);
		return -EINVAL;
	}

	int32_t format_idx = get_format_index(descriptor->alloc_format & MALI_GRALLOC_INTFMT_FMT_MASK);
	if (format_idx == -1)
	{
		return -EINVAL;
	}
	MALI_GRALLOC_LOGV("alloc_format: 0x%" PRIx64 " format_idx: %d", descriptor->alloc_format, format_idx);

	/*
	 * Obtain allocation type (uncompressed, AFBC basic, etc...)
	 */
	if (!get_alloc_type(descriptor->alloc_format & MALI_GRALLOC_INTFMT_EXT_MASK,
	    format_idx, usage, &alloc_type))
	{
		return -EINVAL;
	}

	if (!validate_format(&formats[format_idx], alloc_type, descriptor))
	{
		return -EINVAL;
	}

	/*
	 * Resolution of frame (allocation width and height) might require adjustment.
	 * This adjustment is only based upon specific usage and pixel format.
	 * If using AFBC, further adjustments to the allocation width and height will be made later
	 * based on AFBC alignment requirements and, for YUV, the plane properties.
	 */
	mali_gralloc_adjust_dimensions(descriptor->alloc_format,
	                               usage,
	                               &alloc_width,
	                               &alloc_height);

	/* Obtain buffer size and plane information. */
	calc_allocation_size(alloc_width,
	                     alloc_height,
	                     alloc_type,
	                     formats[format_idx],
	                     usage & (GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK),
	                     usage & ~(GRALLOC_USAGE_PRIVATE_MASK | GRALLOC_USAGE_SW_READ_MASK | GRALLOC_USAGE_SW_WRITE_MASK),
	                     &descriptor->pixel_stride,
	                     &descriptor->size,
	                     descriptor->plane_info);

	/*
	 * Each layer of a multi-layer buffer must be aligned so that
	 * it is accessible by both producer and consumer. In most cases,
	 * the stride alignment is also sufficient for each layer, however
	 * for AFBC the header buffer alignment is more constrained (see
	 * AFBC specification v3.4, section 2.15: "Alignment requirements").
	 * Also update the buffer size to accommodate all layers.
	 */
	if (descriptor->layer_count > 1)
	{
		if (is_format_afbc(descriptor->alloc_format))
		{
			if (descriptor->alloc_format & MALI_GRALLOC_INTFMT_AFBC_TILED_HEADERS)
			{
				descriptor->size = GRALLOC_ALIGN(descriptor->size, 4096);
			}
			else
			{
				descriptor->size = GRALLOC_ALIGN(descriptor->size, 128);
			}
		}

		descriptor->size *= descriptor->layer_count;
	}

	return 0;
}


int mali_gralloc_buffer_allocate(buffer_descriptor_t *descriptor, private_handle_t **out_handle)
{
	int err = mali_gralloc_derive_format_and_size(descriptor);
	if (err != 0)
	{
		return err;
	}

	int ret = allocator_allocate(descriptor, out_handle);
	if (ret != 0)
	{
		return ret;
	}

	(*out_handle)->backing_store_id = getUniqueId();

	return 0;
}

int mali_gralloc_buffer_free(private_handle_t *hnd)
{
	if (hnd == nullptr)
	{
		return -1;
	}

	allocator_free(hnd);
	gralloc_shared_memory_free(hnd->share_attr_fd, hnd->attr_base, hnd->attr_size);
	hnd->share_fd = hnd->share_attr_fd = -1;
	hnd->base = hnd->attr_base = MAP_FAILED;

	return 0;
}
