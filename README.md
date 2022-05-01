# gralloc_valhal_34
gralloc_valhal34

1. 官网下载android-gralloc-module,选择版本 r34p0-01eac0 (Valhall)   
https://developer.arm.com/tools-and-software/graphics-and-gaming/mali-drivers/android-gralloc-module   

2. 将下载的android-gralloc-module放到android/hardware/目录下   

3. 设置编译选项 Enabling build files for Gralloc version 4   
     cd driver/product/android/gralloc   
     ./configure ANDROID_12=y GRALLOC_4=y   

3. 执行 mm 编译  

4. 得到   
/vendor/lib/hw/android.hardware.graphics.allocator@4.0-impl-arm.so   
/vendor/lib/hw/android.hardware.graphics.mapper@4.0-impl-arm.so   
/vendor/lib/arm.graphics-V2-ndk_platform.so     

=====
 
