# gralloc_valhal_34
gralloc_valhal34

打开编译的模块

  renamed:    driver/product/android/gralloc/Android.bp.disabled -> driver/product/android/gralloc/Android.bp
	renamed:    driver/product/android/gralloc/build_compatibility/Android.bp.disabled.12 -> driver/product/android/gralloc/build_compatibility/Android.bp
	renamed:    driver/product/android/gralloc/build_configs/Android.bp.disabled.release -> driver/product/android/gralloc/build_configs/Android.bp
	renamed:    driver/product/android/gralloc/interfaces/aidl/Android.bp.disabled -> driver/product/android/gralloc/interfaces/aidl/Android.bp
	renamed:    driver/product/android/gralloc/interfaces/libs/drmutils/Android.bp.disabled -> driver/product/android/gralloc/interfaces/libs/drmutils/Android.bp
	renamed:    driver/product/android/gralloc/src/4.x/Android.bp.disabled -> driver/product/android/gralloc/src/4.x/Android.bp
	renamed:    driver/product/android/gralloc/src/Android.bp.disabled -> driver/product/android/gralloc/src/Android.bp
	renamed:    driver/product/android/gralloc/src/allocator/Android.bp.disabled -> driver/product/android/gralloc/src/allocator/Android.bp
	renamed:    driver/product/android/gralloc/src/allocator/dma_buf_heaps/Android.bp.disabled -> driver/product/android/gralloc/src/allocator/dma_buf_heaps/Android.bp
	renamed:    driver/product/android/gralloc/src/allocator/shared_memory/Android.bp.disabled -> driver/product/android/gralloc/src/allocator/shared_memory/Android.bp
	renamed:    driver/product/android/gralloc/src/capabilities/Android.bp.disabled -> driver/product/android/gralloc/src/capabilities/Android.bp
	renamed:    driver/product/android/gralloc/src/core/Android.bp.disabled -> driver/product/android/gralloc/src/core/Android.bp
	renamed:    driver/product/android/gralloc/src/hidl_common/Android.bp.disabled -> driver/product/android/gralloc/src/hidl_common/Android.bp

切换到目录driver/product/android/gralloc/src/4.x

执行 mm 编译

得到
/vendor/lib/hw/android.hardware.graphics.allocator@4.0-impl-arm.so
/vendor/lib/hw/android.hardware.graphics.mapper@4.0-impl-arm.so
/vendor/lib/arm.graphics-V2-ndk_platform.so
