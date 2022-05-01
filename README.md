# gralloc_valhal_34
gralloc_valhal34

1.打开编译的模块   

  renamed:    driver/product/android/gralloc/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/build_compatibility/Android.bp.disabled.12
  
  renamed:    driver/product/android/gralloc/build_configs/Android.bp.disabled.release
  
  renamed:    driver/product/android/gralloc/interfaces/aidl/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/interfaces/libs/drmutils/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/4.x/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/allocator/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/allocator/dma_buf_heaps/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/allocator/shared_memory/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/capabilities/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/core/Android.bp.disabled
  
  renamed:    driver/product/android/gralloc/src/hidl_common/Android.bp.disabled
  
  
2. 切换到目录driver/product/android/gralloc/src/4.x   

3. 执行 mm 编译  

4. 得到   
/vendor/lib/hw/android.hardware.graphics.allocator@4.0-impl-arm.so   
/vendor/lib/hw/android.hardware.graphics.mapper@4.0-impl-arm.so   
/vendor/lib/arm.graphics-V2-ndk_platform.so     

=====

以上修改本质其实可以通过configure来设置：   
1. 官网下载android-gralloc-module,选择版本 r34p0-01eac0 (Valhall)   
https://developer.arm.com/tools-and-software/graphics-and-gaming/mali-drivers/android-gralloc-module   

2. 将下载的android-gralloc-module放到android/hardware/realtek/目录下   

3. 设置编译选项 Enabling build files for Gralloc version 4   
    # cd driver/product/android/gralloc   
    # ./configure ANDROID_12=y GRALLOC_4=y    
