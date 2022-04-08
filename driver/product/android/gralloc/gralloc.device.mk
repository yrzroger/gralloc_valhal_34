# Copyright (C) 2020-2021 Arm Limited.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Configuration that should be included by BoardConfig.mk to configure necessary Soong namespaces.

#
# Software behaviour defines
#

# The following defines are used to override default behaviour of which heap is selected for allocations.
# The default is to pick system heap.
# The following two defines enable either DMA heap or compound page heap for when the usage has
# GRALLOC_USAGE_HW_FB or GRALLOC_USAGE_HW_COMPOSER set and GRALLOC_USAGE_HW_VIDEO_ENCODER is not set.
# These defines should not be enabled at the same time.
GRALLOC_USE_ION_DMA_HEAP?=0
GRALLOC_USE_ION_COMPOUND_PAGE_HEAP?=0

# Properly initializes an empty AFBC buffer
GRALLOC_INIT_AFBC?=0
# When enabled, forces format to BGRA_8888 for FB usage when HWC is in use
GRALLOC_HWC_FORCE_BGRA_8888?=0
# When enabled, disables AFBC for FB usage when HWC is in use
GRALLOC_HWC_FB_DISABLE_AFBC?=0

# When enabled, buffers will never be allocated with AFBC
GRALLOC_ARM_NO_EXTERNAL_AFBC?=0

# When enabled, sets camera capability bit
GRALLOC_CAMERA_WRITE_RAW16?=1

ifeq ($(GRALLOC_USE_ION_DMA_HEAP), 1)
ifeq ($(GRALLOC_USE_ION_COMPOUND_PAGE_HEAP), 1)
$(error "GRALLOC_USE_ION_DMA_HEAP and GRALLOC_USE_ION_COMPOUND_PAGE_HEAP can't be enabled at the same time")
endif
endif

# Setup configuration in Soong namespace
SOONG_CONFIG_NAMESPACES += arm_gralloc
SOONG_CONFIG_arm_gralloc := \
	gralloc_use_ion_dma_heap \
	gralloc_use_ion_compound_page_heap \
	gralloc_init_afbc \
	gralloc_hwc_force_bgra_8888 \
	gralloc_hwc_fb_disable_afbc \
	gralloc_arm_no_external_afbc \
	gralloc_camera_write_raw16

SOONG_CONFIG_arm_gralloc_gralloc_use_ion_dma_heap := $(GRALLOC_USE_ION_DMA_HEAP)
SOONG_CONFIG_arm_gralloc_gralloc_use_ion_compound_page_heap := $(GRALLOC_USE_ION_COMPOUND_PAGE_HEAP)
SOONG_CONFIG_arm_gralloc_gralloc_init_afbc := $(GRALLOC_INIT_AFBC)
SOONG_CONFIG_arm_gralloc_gralloc_hwc_force_bgra_8888 := $(GRALLOC_HWC_FORCE_BGRA_8888)
SOONG_CONFIG_arm_gralloc_gralloc_hwc_fb_disable_afbc := $(GRALLOC_HWC_FB_DISABLE_AFBC)
SOONG_CONFIG_arm_gralloc_gralloc_arm_no_external_afbc := $(GRALLOC_ARM_NO_EXTERNAL_AFBC)
SOONG_CONFIG_arm_gralloc_gralloc_camera_write_raw16 := $(GRALLOC_CAMERA_WRITE_RAW16)
