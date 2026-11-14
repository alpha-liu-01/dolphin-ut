# Stage 0: probing the phone

Stage 0 of `roadmap.md`: check the pieces Dolphin will lean on, before any Dolphin code runs on the phone.

Started 2026-11-14 on the stone phone: Ubuntu Touch 24.04-2.x, kernel `#20`, Lomiri 0.5.0, Qt 6.10.2, libhybris `0.1.0+git20260624+7079712`.

How it was done:
- Everything ran as phablet over `adb shell`, with no root and nothing installed. Two `.deb` files were downloaded and unpacked into the probe folder (step 1).
- The automated probes ran with the screen **off and locked**. The user then ran the on-screen and gamepad tests the same day (see "Manual tests").
- The phone has no compiler, no `wayland-info`, `eglinfo` or `vulkaninfo`, and no PyQt. The probe is `ut/phone-debug/wayland-probe.py`: Python ctypes on `libwayland-client`, glvnd's `libEGL.so.1` and `libvulkan.so.1`.
  - Run it as `XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 python3 wayland-probe.py <mode>`.
  - The modes are `globals`, `egl`, `egl-window [--fullscreen] [--frames N]`, `vulkan` and `vulkan-window`.
  - It re-executes itself with `GLIBC_TUNABLES=glibc.rtld.optional_static_tls=65536`, which libhybris needs inside Python.
- Probe folder on the phone: `/home/phablet/dolphin-ut-probe/` (the script, unpacked Qt packages, logs).

## Results

| # | Question | Answer |
| --- | --- | --- |
| 1 | Does Qt 6 run natively on Lomiri's Wayland? | **Not as installed.** The `wayland` plugin is there, but no shell integration plugin is. With `libxdg-shell.so` from UBports' `libqt6wlshellintegration6`, Qt 6 uses xdg-shell + Wayland-EGL and creates its toplevel. A Widgets window is not tested yet (no Qt 6 test binary) |
| 1a | Which Wayland globals does Lomiri offer? | Few. `xdg_wm_base` **v1**, `zxdg_shell_v6`, `wl_shell`, `wl_subcompositor`, `wl_seat` v6 (pointer, keyboard, touch), `wl_output` v3, `android_wlegl` v2, `qt_windowmanager`. None of #14962's optional protocols |
| 1b | What does a click app get as `QT_QPA_PLATFORM`? | `ubuntumirclient;wayland-egl;xcb`, confirmed on a launched click app |
| 2 | Toplevel and fullscreen | Windowed: 1080x2305 (activated), below the panel. Fullscreen: 1080x2400. Both shown on screen (M1). The 2305 fullscreen reconfigure happened only with the screen off |
| 3 | GLES on the Wayland display, as #14962 does it | **Yes.** `eglGetDisplay(wl_display)` → glvnd → `libEGL_libhybris` → `eglplatform_wayland.so` → Adreno, GLES 3.2. Shared contexts on another thread work (pbuffer and surfaceless). Window surface, resize and recreate work, also on screen (M1). Swap-interval-1 pacing is 83–87 fps on the 120 Hz panel, not locked to vsync (stage 4) |
| 4 | Vulkan on a Wayland surface | **Yes**, with one catch. `VK_KHR_wayland_surface` → `vulkanplatform_wayland.so`. Surface, swapchain and acquire work, and `test_vulkan` presents on screen (M2). `currentExtent` is **1x1**, which breaks #14962's size logic |
| 5 | Gamepads | The kernel has the drivers (uhid, xpad, PlayStation, Nintendo, Steam, Microsoft). A Bluetooth BETOP T6 (Xbox mode, `045e:02e0`) binds `hid-microsoft`, is tagged as a joystick, has rumble, and phablet can open it (M3). Its buttons use generic codes. USB not tested |

## 1. Qt 6 on Lomiri's Wayland

### Environment of a click app

Calculator was launched with `lomiri-app-launch calculator.ubports_calculator_4.1.0` and its process environment read back. It ran as `calculator.ubports_calculator_4.1.0 (enforce)`.

```
QT_QPA_PLATFORM=ubuntumirclient;wayland-egl;xcb
QT_WAYLAND_DISABLE_WINDOWDECORATION=1
QT_OPENGL_VARIANT=gles
QT_IM_MODULE=maliit
QT_SCALE_FACTOR_ROUNDING_POLICY=PassThrough
QT_QUICK_CONTROLS_MOBILE=1
WAYLAND_DISPLAY=wayland-0
MIR_SOCKET=/run/user/32011/mir_socket
XDG_SESSION_TYPE=mir
GDK_GL=gles
```

`EGL_PLATFORM` is not set. The systemd unit (`lomiri-app-launch--application-click--<appid>--.service`) runs the app through `aa-exec -p <appid>`. Qt 6 here ships `libqpa-ubuntumirclient.so`, so a Qt 6 Dolphin would pick `ubuntumirclient` first. Dolphin has to force `wayland` (roadmap stage 2 step 3).

### Wayland globals (`wayland-probe.py globals`)

```
android_wlegl v2            qt_windowmanager v1      wl_compositor v4
wl_data_device_manager v3   wl_output v3             wl_seat v6
wl_shell v1                 wl_shm v1                wl_subcompositor v1
xdg_wm_base v1              zxdg_shell_v6 v1
wl_output: 1080x2400 @ 120.00 Hz (current, preferred), 70x155 mm, scale 1, "Fake manufacturer"
wl_seat 'seat0': pointer, keyboard, touch
```

Not offered: `zwp_linux_dmabuf_v1`, `wp_viewporter`, `wp_content_type_manager_v1`, `zwp_pointer_constraints_v1`, `zwp_relative_pointer_manager_v1`, `wp_pointer_warp_v1`, `zxdg_decoration_manager_v1`, `wp_fractional_scale_manager_v1`, `wp_presentation`, `zxdg_output_manager_v1`, text-input v2/v3, idle-inhibit, `xdg_activation_v1`, `wp_tearing_control_manager_v1`.

What this means:
- #14962's content-type (VRR hint), pointer-constraints, relative-pointer and pointer-warp binds will all find nothing. The code has to tolerate that; check it in stage 2.
- `xdg_wm_base` v1 has no `configure_bounds`, no `wm_capabilities` and no tiled or suspended states.
- ROCKNIX's NoGUI window (patch 015) needs only `xdg_wm_base`, which is present.
- No text-input protocol: the on-screen keyboard reaches Qt through `QT_IM_MODULE=maliit`, not Wayland.

### Qt 6 shell integration

Morph Browser is the only Qt 6 program installed. Started with `QT_QPA_PLATFORM=wayland`, it failed:

```
qt.qpa.wayland: No shell integration named "xdg-shell" found
qt.qpa.wayland: No shell integration named "wl-shell" found
qt.qpa.wayland: No shell integration named "ivi-shell" found
qt.qpa.wayland: No shell integration named "qt-shell" found
qt.qpa.plugin: Could not load the Qt platform plugin "wayland" in "" even though it was found.
```

- `qt6/plugins/wayland-shell-integration/` on the phone holds only maliit's `libinputpanel-shell.so`.
- In Qt 6.10, xdg-shell is a plugin, not part of `libQt6WaylandClient`.
- UBports' `Contents-arm64` lists `libxdg-shell.so` and `libwl-shell-plugin.so` in **`libqt6wlshellintegration6`** (6.10.2+dfsg-14, which also carries `libQt6WlShellIntegration.so.6`) and in `qt6-wayland`. The `qt6-wayland` 6.10.2-5 that `apt-get download` fetched had only ivi-shell and qt-shell. Neither package is installed.

Morph is a poor test program: it has a path-attached AppArmor profile (`/etc/apparmor.d/usr.bin.morph-browser`), so it is confined even when started from a shell. It can only map libraries from system paths or from `~/.local/share/morph-browser/**`. Plugins in the probe folder were silently skipped: the factory loader logged "checking directory path" and then found no files. For one run, `libxdg-shell.so` and `libQt6WlShellIntegration.so.6` were copied into `~/.local/share/morph-browser/dolphin-ut-probe-tmp/` (removed afterwards). With `QT_PLUGIN_PATH` and `LD_LIBRARY_PATH` pointing there:

```
qt.qpa.wayland: Using the 'xdg-shell' shell integration
qt.qpa.wayland: Available client buffer integrations: QList("wayland-egl")
qt.qpa.wayland: Using Wayland-EGL
-> wl_registry.bind(10, "xdg_wm_base", 1, ...)    -> wl_registry.bind(1, "android_wlegl", 2, ...)
-> xdg_wm_base.get_xdg_surface   -> xdg_surface.get_toplevel   -> xdg_toplevel.set_title("Morph Web Browser")
-> wl_surface.commit()
```

Then Morph aborted. QtWebEngine's Chromium GPU process could not initialize EGL ("Failed to get system egl display", "Initialization of all EGL display types failed"). That is Morph's web engine, not Qt's windowing, and it does not apply to Dolphin.

For the port:
- The click package must **bundle** `wayland-shell-integration/libxdg-shell.so` and `libQt6WlShellIntegration.so.6`, built for the same Qt 6.10.2, and point `QT_PLUGIN_PATH` at them. Click apps may map libraries from their own install directory.
- Alternatively, the system image could install `libqt6wlshellintegration6`. That is a request to UBports, not something an app can rely on.

Not tested yet: a Qt 6 **Widgets** window (raster or `QOpenGLWidget`) actually mapping and taking touch input. This needs a small Qt 6 test program built in the stage 1 container, against the phone's Qt 6.10.2.

## 2. Toplevel windows (`wayland-probe.py egl-window`)

The probe's own `xdg_toplevel` (xdg_wm_base v1), screen off:

| Request | Configure events |
| --- | --- |
| Normal toplevel | `0x0 []`, then `0x0 [activated]`, then `1080x2305 [activated]` |
| `set_fullscreen(NULL)` before the first commit | `0x0 []`, `0x0 [fullscreen]`, `1080x2400 [fullscreen]`, `1080x2400 [activated, fullscreen]`, then **`1080x2305 [activated, fullscreen]`** |

- 2305 = 2400 minus Lomiri's 95 px indicator panel.
- The first configure is 0x0 ("client decides"). Dolphin and #14962 must not size the surface from it. The probe used the output size until a real size arrived.
- Fullscreen shrinking back to 2305 once the window was activated happened only with the screen off. With the screen on (M1), the last configure was `1080x2400 [activated, fullscreen]`.

## 3. GLES through EGL on the Wayland display (`wayland-probe.py egl`)

```
eglGetDisplay(wl_display) + eglInitialize: OK, EGL 1.5
EGL_VENDOR             Android
EGL_VERSION            1.5 Android META-EGL
EGL_CLIENT_APIS        OpenGL_ES
libhybris EGL platforms loaded: ['/usr/lib/aarch64-linux-gnu/libhybris/eglplatform_wayland.so']
EGL vendor libraries loaded: libEGL_libhybris.so.0.0.0, libEGL_adreno.so (and libEGL_mesa, loaded by glvnd but not used)
GLES3 configs: window 68, pbuffer 68         EGL_KHR_surfaceless_context: yes
GL_RENDERER  Adreno (TM) 619
GL_VERSION   OpenGL ES 3.2 V@0530.0 (GIT@c263890aed, Ifb496a9c37, 1656315076) (Date:06/27/22)
GLSL         OpenGL ES GLSL ES 3.20
shared context on a second thread: created, current on a pbuffer, current surfaceless
```

So #14962's `GLContextEGLWayland::OpenEGLDisplay()` (`eglGetDisplay` on the `wl_display`) reaches libhybris' Wayland platform through glvnd with no extra environment. `EGL_PLATFORM` is not needed. Dolphin's shader-compile threads get shared contexts.

Client extensions include `EGL_EXT_platform_wayland`, `EGL_KHR_platform_wayland` and `EGL_KHR_platform_android`. Display extensions (47) include `EGL_KHR_create_context`, `EGL_KHR_no_config_context`, `EGL_KHR_surfaceless_context`, `EGL_KHR_swap_buffers_with_damage`, `EGL_ANDROID_native_fence_sync`, `EGL_KHR_fence_sync`, `EGL_KHR_wait_sync`, `EGL_ANDROID_presentation_time`, `EGL_ANDROID_get_frame_timestamps`, `EGL_WL_bind_wayland_display`, `EGL_HYBRIS_native_buffer2`.

### Limits

| Limit | Value | Limit | Value |
| --- | --- | --- | --- |
| `GL_MAX_TEXTURE_SIZE` | 16384 | `GL_MAX_ARRAY_TEXTURE_LAYERS` | 2048 |
| `GL_MAX_SAMPLES` | 4 | `GL_MAX_COLOR/DEPTH_TEXTURE_SAMPLES` | 4 / 4 |
| `GL_MAX_UNIFORM_BLOCK_SIZE` | 65536 | `GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT` | 32 |
| `GL_MAX_SHADER_STORAGE_BLOCK_SIZE` | 536870912 | `GL_MAX_FRAGMENT/VERTEX_SHADER_STORAGE_BLOCKS` | 4 / 4 |
| `GL_MAX_TEXTURE_BUFFER_SIZE` | 134217728 | `GL_MAX_COMPUTE_SHARED_MEMORY_SIZE` | 32768 |
| `GL_MAX_GEOMETRY_OUTPUT_VERTICES` | 1024 | `GL_MAX_CLIP_DISTANCES` | 8 |
| `GL_NUM_PROGRAM_BINARY_FORMATS` | 1 | `GL_MAX_DUAL_SOURCE_DRAW_BUFFERS` | 1 |

### Extensions Dolphin asks for

From `GLExtensions::Supports()` in the OGL backend, GLES-relevant ones only:
- **Present:** `GL_ANDROID_extension_pack_es31a`, `GL_EXT_blend_func_extended` (dual-source blending), `GL_EXT_buffer_storage`, `GL_EXT_copy_image`, `GL_EXT_shader_framebuffer_fetch`, `GL_EXT_texture_buffer`, `GL_EXT_texture_filter_anisotropic`, `GL_KHR_debug`, `GL_OES_texture_storage_multisample_2d_array`, `GL_EXT_clip_cull_distance`, `GL_EXT_geometry_shader`, `GL_EXT_texture_border_clamp`, `GL_EXT_primitive_bounding_box`, `GL_EXT_shader_io_blocks`, `GL_OES_get_program_binary`, `GL_EXT_clip_control`, `GL_EXT_disjoint_timer_query`, `GL_KHR_texture_compression_astc_ldr`, `GL_EXT_multisampled_render_to_texture`, `GL_OES_sample_shading`, `GL_OES_shader_multisample_interpolation`, `GL_EXT_texture_format_BGRA8888`, `GL_EXT_color_buffer_float`, `GL_EXT_color_buffer_half_float`.
- **Absent:** `GL_ARM_shader_framebuffer_fetch`, `GL_EXT_draw_elements_base_vertex` / `GL_OES_draw_elements_base_vertex` (core in GLES 3.2), `GL_EXT_geometry_point_size` / `GL_OES_geometry_point_size`, `GL_EXT_shader_pixel_local_storage`, `GL_EXT_texture_compression_s3tc`, `GL_EXT_texture_compression_bptc`, `GL_KHR_shader_subgroup`, `GL_NV_*`, `GL_OES_copy_image`, `GL_OES_texture_buffer`, `GL_OES_geometry_shader` (the EXT one is present), `GL_EXT_depth_clamp`.

This is the same driver Android Dolphin runs on this SoC. Whether a missing item matters is a stage 4 question.

All 98 GL extensions:

```
GL_AMD_compressed_ATC_texture GL_ANDROID_extension_pack_es31a GL_ARM_shader_framebuffer_fetch_depth_stencil
GL_EXT_EGL_image_array GL_EXT_EGL_image_external_wrap_modes GL_EXT_EGL_image_storage GL_EXT_YUV_target
GL_EXT_blend_func_extended GL_EXT_blit_framebuffer_params GL_EXT_buffer_storage GL_EXT_clip_control
GL_EXT_clip_cull_distance GL_EXT_color_buffer_float GL_EXT_color_buffer_half_float GL_EXT_copy_image
GL_EXT_debug_label GL_EXT_debug_marker GL_EXT_discard_framebuffer GL_EXT_disjoint_timer_query
GL_EXT_draw_buffers_indexed GL_EXT_external_buffer GL_EXT_fragment_invocation_density GL_EXT_geometry_shader
GL_EXT_gpu_shader5 GL_EXT_memory_object GL_EXT_memory_object_fd GL_EXT_multisampled_render_to_texture
GL_EXT_multisampled_render_to_texture2 GL_EXT_primitive_bounding_box GL_EXT_protected_textures
GL_EXT_read_format_bgra GL_EXT_robustness GL_EXT_sRGB GL_EXT_sRGB_write_control GL_EXT_shader_framebuffer_fetch
GL_EXT_shader_io_blocks GL_EXT_shader_non_constant_global_initializers GL_EXT_tessellation_shader
GL_EXT_texture_border_clamp GL_EXT_texture_buffer GL_EXT_texture_cube_map_array GL_EXT_texture_filter_anisotropic
GL_EXT_texture_format_BGRA8888 GL_EXT_texture_format_sRGB_override GL_EXT_texture_norm16 GL_EXT_texture_sRGB_R8
GL_EXT_texture_sRGB_decode GL_EXT_texture_type_2_10_10_10_REV GL_KHR_blend_equation_advanced
GL_KHR_blend_equation_advanced_coherent GL_KHR_debug GL_KHR_no_error GL_KHR_robust_buffer_access_behavior
GL_KHR_texture_compression_astc_hdr GL_KHR_texture_compression_astc_ldr GL_NV_shader_noperspective_interpolation
GL_OES_EGL_image GL_OES_EGL_image_external GL_OES_EGL_image_external_essl3 GL_OES_EGL_sync
GL_OES_compressed_ETC1_RGB8_texture GL_OES_depth24 GL_OES_depth_texture GL_OES_depth_texture_cube_map
GL_OES_element_index_uint GL_OES_framebuffer_object GL_OES_get_program_binary GL_OES_packed_depth_stencil
GL_OES_rgb8_rgba8 GL_OES_sample_shading GL_OES_sample_variables GL_OES_shader_image_atomic
GL_OES_shader_multisample_interpolation GL_OES_standard_derivatives GL_OES_surfaceless_context GL_OES_texture_3D
GL_OES_texture_compression_astc GL_OES_texture_float GL_OES_texture_float_linear GL_OES_texture_half_float
GL_OES_texture_half_float_linear GL_OES_texture_npot GL_OES_texture_stencil8
GL_OES_texture_storage_multisample_2d_array GL_OES_texture_view GL_OES_vertex_array_object
GL_OES_vertex_half_float GL_OVR_multiview GL_OVR_multiview2 GL_OVR_multiview_multisampled_render_to_texture
GL_QCOM_YUV_texture_gather GL_QCOM_alpha_test GL_QCOM_shader_framebuffer_fetch_noncoherent
GL_QCOM_shader_framebuffer_fetch_rate GL_QCOM_texture_foveated GL_QCOM_texture_foveated_subsampled_layout
GL_QCOM_tiled_rendering GL_QCOM_validate_shader_binary
```

### EGL window surface (screen off)

`egl-window` on the probe's toplevel: a `wl_egl_window` at 1080x2400, a GLES 3 window config, a context shared with the pbuffer one.

| Step | Result |
| --- | --- |
| `eglCreateWindowSurface` + `eglMakeCurrent` | OK |
| 30 swaps, interval 1 | 72–82 fps, no failed swaps |
| 30 swaps, interval 0 | about 240 fps |
| `wl_egl_window_resize` to 540x1200 | `eglQuerySurface` reports 540x1200 on the next frame; swaps OK |
| resize back to 1080x2400 | OK |
| destroy the surface and `wl_egl_window`, create them again | OK, 60 swaps |

- With the screen off, interval-1 swaps do **not** block, and the rate does not follow the panel. Those fps numbers say nothing about pacing. With the screen on (M1), the rate was 83–87 fps, also not locked to the 120 Hz panel.
- `eglQuerySurface` follows `wl_egl_window_resize`. Dolphin's `GLContext` gets its backbuffer size that way (`43fe02ee9b`), so resizing should work with #14962's `Update()`.

## 4. Vulkan (`wayland-probe.py vulkan`, `vulkan-window`)

The loader is libhybris' `libvulkan.so.1.2.183`, which loads `/android/system/lib64/libvulkan.so`.

Instance extensions (13): `VK_EXT_debug_report VK_EXT_surface_maintenance1 VK_EXT_swapchain_colorspace VK_GOOGLE_surfaceless_query VK_KHR_device_group_creation VK_KHR_external_fence_capabilities VK_KHR_external_memory_capabilities VK_KHR_external_semaphore_capabilities VK_KHR_get_physical_device_properties2 VK_KHR_get_surface_capabilities2 VK_KHR_surface VK_KHR_surface_protected_capabilities VK_KHR_wayland_surface`.
- No `VK_EXT_debug_utils`, so Dolphin's debug labels and validation callback are unavailable.
- No `VK_KHR_xlib_surface`, as expected.
- No validation layers.

Device: `Adreno (TM) 619`, API **1.1.128**, driver `0x80212000`, vendor `0x5143`, device `0x6010901`, integrated. The driver ID is 8, "Qualcomm Technologies Inc. Adreno Vulkan Driver", build `c263890aed`, 06/27/22, compiler EV031.35.01.12.

### What Dolphin reads

| Feature | | Feature | |
| --- | --- | --- | --- |
| `depthClamp` | yes | `dualSrcBlend` | yes |
| `fragmentStoresAndAtomics` | yes | `geometryShader` | yes |
| `largePoints` | yes | **`logicOp`** | **no** |
| `occlusionQueryPrecise` | yes | `samplerAnisotropy` | yes |
| `sampleRateShading` | yes | `shaderClipDistance` | yes |
| **`shaderStorageImageMultisample`** | **no** | **`shaderTessellationAndGeometryPointSize`** | **no** |
| **`textureCompressionBC`** | **no** (ETC2 and ASTC LDR yes) | | |

- **Subgroups:** size 64, operations basic, vote, arithmetic, ballot, shuffle and shuffle_relative, but only in **compute** (`supportedStages` 0x20). Dolphin's `shaderSubgroupOperations` needs the fragment stage, so it will be **off**.
- **Device extensions Dolphin looks for:**
  - present: `VK_KHR_swapchain`, `VK_KHR_driver_properties`, `VK_KHR_shader_draw_parameters`, `VK_KHR_dedicated_allocation`, `VK_KHR_get_memory_requirements2`, `VK_KHR_maintenance1/2/3`;
  - **absent:** `VK_EXT_memory_budget`, `VK_EXT_depth_range_unrestricted`, `VK_EXT_depth_clamp_control`, `VK_EXT_custom_border_color`.
- **Limits:** `maxImageDimension2D` 16384, `maxTexelBufferElements` 134217728, `maxUniformBufferRange` 65536, `maxPushConstantsSize` 128, `minUniformBufferOffsetAlignment` 64, `bufferImageGranularity` 1, `optimalBufferCopyRowPitchAlignment` 64, color/depth sample counts 0x7 (1/2/4), `maxSamplerAnisotropy` 16, `pointSizeRange` 1–4092, `timestampPeriod` 52.08 ns.
- **Formats** (optimal tiling): D16, D32F, D24S8 and D32FS8 are all depth attachments. RGBA8, BGRA8, A2B10G10R10, RGBA16F and R32F are color attachments. BC1 has no support (consistent with `textureCompressionBC`).

All 63 device extensions: `VK_ANDROID_external_memory_android_hardware_buffer VK_EXT_blend_operation_advanced VK_EXT_device_memory_report VK_EXT_global_priority VK_EXT_hdr_metadata VK_EXT_host_query_reset VK_EXT_image_robustness VK_EXT_index_type_uint8 VK_EXT_line_rasterization VK_EXT_pipeline_creation_cache_control VK_EXT_pipeline_creation_feedback VK_EXT_private_data VK_EXT_queue_family_foreign VK_EXT_sampler_filter_minmax VK_EXT_scalar_block_layout VK_EXT_separate_stencil_usage VK_EXT_shader_demote_to_helper_invocation VK_EXT_subgroup_size_control VK_EXT_swapchain_maintenance1 VK_EXT_texture_compression_astc_hdr VK_EXT_vertex_attribute_divisor VK_GOOGLE_display_timing VK_KHR_bind_memory2 VK_KHR_buffer_device_address VK_KHR_create_renderpass2 VK_KHR_dedicated_allocation VK_KHR_descriptor_update_template VK_KHR_device_group VK_KHR_draw_indirect_count VK_KHR_driver_properties VK_KHR_external_fence VK_KHR_external_fence_fd VK_KHR_external_memory VK_KHR_external_memory_fd VK_KHR_external_semaphore VK_KHR_external_semaphore_fd VK_KHR_get_memory_requirements2 VK_KHR_image_format_list VK_KHR_imageless_framebuffer VK_KHR_incremental_present VK_KHR_maintenance1 VK_KHR_maintenance2 VK_KHR_maintenance3 VK_KHR_multiview VK_KHR_push_descriptor VK_KHR_relaxed_block_layout VK_KHR_sampler_mirror_clamp_to_edge VK_KHR_sampler_ycbcr_conversion VK_KHR_separate_depth_stencil_layouts VK_KHR_shader_draw_parameters VK_KHR_shader_float16_int8 VK_KHR_shader_float_controls VK_KHR_shader_subgroup_extended_types VK_KHR_shared_presentable_image VK_KHR_spirv_1_4 VK_KHR_storage_buffer_storage_class VK_KHR_swapchain VK_KHR_uniform_buffer_standard_layout VK_KHR_variable_pointers VK_KHR_vulkan_memory_model VK_QCOM_render_pass_shader_resolve VK_QCOM_render_pass_store_ops VK_QCOM_render_pass_transform`.

### Wayland surface and swapchain (screen off)

```
vkCreateWaylandSurfaceKHR: 0            libhybris Vulkan platform: vulkanplatform_wayland.so
queue family 0: graphics+compute+transfer, 3 queues, present support yes
surface capabilities: images 3..64, currentExtent 1x1, min 1x1, max 4096x4096,
                      transforms 0x1ff (current identity), compositeAlpha 0x8 (INHERIT only), usage 0x9f
surface formats: R8G8B8A8_UNORM, R8G8B8A8_SRGB, R5G6B5_UNORM_PACK16, R16G16B16A16_SFLOAT, A2B10G10R10_UNORM_PACK32
                 (all SRGB_NONLINEAR; no B8G8R8A8)
present modes: MAILBOX, FIFO, SHARED_DEMAND_REFRESH, SHARED_CONTINUOUS_REFRESH (no IMMEDIATE)
vkCreateSwapchainKHR FIFO 1x1: 0, 3 images; vkAcquireNextImageKHR: 0
Dolphin-style swapchains, 1080x2400, FIFO and MAILBOX, compositeAlpha OPAQUE (0x1) and INHERIT (0x8): all 0
```

For the port:
- **`currentExtent` is 1x1, not `0xFFFFFFFF`.** #14962's `SwapChain::CreateSwapChain()` uses the window's size only when `currentExtent.width == UINT32_MAX`. Here it would create a **1x1 swapchain**. A port commit on top of the import must use the window's reported size whenever the window system is Wayland.
- **Composite alpha.** Dolphin passes `VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR` (upstream code, not from #14962), but the surface reports only INHERIT. libhybris accepts OPAQUE anyway, so no change is needed. It would be a validation error if validation layers ever ran.
- **No IMMEDIATE.** Dolphin's "VSync off" will get MAILBOX. That needs no change.
- **No B8G8R8A8 surface format.** Dolphin's swapchain format selection must accept R8G8B8A8_UNORM; check in stage 2.
- Presenting on a visible window: libhybris' `test_vulkan` presented and animated on screen (M2).

## 5. Gamepads (kernel side only)

From `/proc/config.gz`:
- `CONFIG_HID`, `CONFIG_UHID` (Bluetooth LE / HOGP pads through BlueZ), `CONFIG_HIDRAW`, `CONFIG_HID_GENERIC`, `CONFIG_USB_HID`, `CONFIG_BT_HIDP`, `CONFIG_INPUT_EVDEV`, `CONFIG_INPUT_UINPUT`, `CONFIG_INPUT_FF_MEMLESS`, `CONFIG_HID_BATTERY_STRENGTH`;
- device drivers: `CONFIG_JOYSTICK_XPAD`, `CONFIG_HID_PLAYSTATION`, `CONFIG_HID_SONY` + `CONFIG_SONY_FF`, `CONFIG_HID_NINTENDO`, `CONFIG_HID_STEAM`, `CONFIG_HID_MICROSOFT`, `CONFIG_HID_LOGITECH`;
- `CONFIG_HID_WIIMOTE=y`;
- `CONFIG_INPUT_JOYDEV` is off. Dolphin uses evdev and SDL, not `/dev/input/js*`, so that does not matter.

`/lib/udev/rules.d` has `70-joystick.rules`, `70-uaccess.rules`, `60-input-id.rules` and `80-libinput-device-groups.rules`. `/dev/input/event*` is `root:android_input 0660`, and phablet is in `android_input`.

Note for later: with `hid-wiimote` built in, the kernel takes real Wii Remotes as input devices when they connect. Dolphin's real-Wiimote support on Linux talks to them over BlueZ L2CAP itself, so the two may conflict. This is not stage 0 work.

## Manual tests (2026-11-14)

Run by the user from a PC over USB, with the screen on and the phone unlocked. The user's raw terminal output was pasted here and is condensed below. The EGL, GL and extension lines printed before each window run were identical to section 3, so they are left out.

### M1. On-screen EGL window

```sh
adb shell 'cd /home/phablet/dolphin-ut-probe; XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 python3 wayland-probe.py egl-window --frames 480'
adb shell 'cd /home/phablet/dolphin-ut-probe; XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 python3 wayland-probe.py egl-window --fullscreen --frames 480'
```

The user reported that both windows appeared on the screen as expected.

Windowed (the user ran it twice, with identical output):

```
configure events        1 (serials [148])
  toplevel configure    0x0 states=[]
window size used        1080x2400
libhybris EGL platforms loaded: ['/usr/lib/aarch64-linux-gnu/libhybris/eglplatform_wayland.so']
vsync run                480 frames in 5.81 s = 82.6 fps (swap interval 1, surface 1080x2400, failed swaps 0)
no-vsync run             480 frames in 1.50 s = 319.5 fps (swap interval 0, surface 1080x2400, failed swaps 0)
after resize to half     30 frames in 0.33 s = 90.3 fps (swap interval 1, surface 540x1200, failed swaps 0)
after resize back        30 frames in 0.35 s = 85.4 fps (swap interval 1, surface 1080x2400, failed swaps 0)
surface recreate        OK
after recreate           60 frames in 0.65 s = 93.0 fps (swap interval 1, surface 1080x2400, failed swaps 0)
configure events        3 (serials [148, 149, 150])
  toplevel configure    0x0 states=[]
  toplevel configure    0x0 states=['activated']
  toplevel configure    1080x2305 states=['activated']
```

Fullscreen:

```
configure events        3 (serials [151, 152, 153])
  toplevel configure    0x0 states=[]
  toplevel configure    0x0 states=['fullscreen']
  toplevel configure    1080x2400 states=['fullscreen']
vsync run                480 frames in 5.54 s = 86.7 fps (swap interval 1, surface 1080x2400, failed swaps 0)
no-vsync run             480 frames in 1.83 s = 262.0 fps (swap interval 0, surface 1080x2400, failed swaps 0)
after resize to half     30 frames in 0.30 s = 98.4 fps (swap interval 1, surface 540x1200, failed swaps 0)
after resize back        30 frames in 0.33 s = 90.1 fps (swap interval 1, surface 1080x2400, failed swaps 0)
surface recreate        OK
after recreate           60 frames in 0.67 s = 89.8 fps (swap interval 1, surface 1080x2400, failed swaps 0)
configure events        4 (serials [151, 152, 153, 154])
  toplevel configure    0x0 states=[]
  toplevel configure    0x0 states=['fullscreen']
  toplevel configure    1080x2400 states=['fullscreen']
  toplevel configure    1080x2400 states=['activated', 'fullscreen']
```

What it shows:
- **EGL on a visible Wayland window works**: no failed swaps, and resize and recreate work on screen as well.
- **Fullscreen keeps 1080x2400** once activated, with the screen on. The reconfigure to 1080x2305 seen with the screen off (section 2) did not happen, so that was a screen-off effect.
- **Pacing is not locked to the panel.** With swap interval 1 the window ran at 83–87 fps on the 120 Hz panel: neither 120 nor an even 60. With the screen off it was 72–82 fps (section 3), so the swap rate follows the frame callbacks from Lomiri's render loop, not the panel's vsync. The probe's own `wl_display_roundtrip()` every 30 frames costs a little, but not 30 fps. Dolphin limits its own speed to the game's 60 (or 50) fps, so this matters as frame pacing: does every game frame stay on screen for exactly two 120 Hz vsyncs? That is a stage 4 measurement with `../stone-ut/tools/phone-debug/fps-panel.py`, which reads the panel's committed frames.
- Unthrottled rendering reaches 260–320 fps at 1080x2400 for a full-screen clear, so the swap path itself is not a bottleneck.

### M2. On-screen Vulkan (libhybris `test_vulkan`)

```
Found 13 instance extensions
Found 1 physical device(s)
  Device name: Adreno (TM) 619
  Device type: Integrated GPU
  API version: 1.1.128
  Driver version: 512.530.0
  Device ID 100731137
  Vendor ID 20803
  Found 63 device extensions
Start render loop
Frame 0
Frame 60
...
Frame 3540
```

The user reported that the window appeared and animated. **Vulkan presentation on a visible Wayland window works**, through `vulkanplatform_wayland.so`. `test_vulkan` does not print a frame rate. Vulkan pacing is a stage 4 measurement like M1.

### M3. Gamepads

Tested: a **BETOP T6** wireless controller over Bluetooth. A wired controller was not tested: the phone's USB port was in use for adb.

`/proc/bus/input/devices` (controller entry only; the other seven are the phone's own keys, touch, jacks and fingerprint):

```
I: Bus=0005 Vendor=045e Product=02e0 Version=0903
N: Name="BETOP T6"
P: Phys=56:37:dc:86:6a:a8
S: Sysfs=/devices/virtual/bluetooth/hci0/hci0:2/0005:045E:02E0.0001/input/input7
H: Handlers=kbd event7 cpufreq
B: EV=20001b
B: KEY=3ff000000000000 0 800 0 0
B: ABS=3003f
B: MSC=10
B: FF=107030000 0
```

Read back over adb while it was still connected:

```
/dev/input/event7     crw-rw---- root android_input 13, 71
driver                /sys/bus/hid/drivers/microsoft
ID_INPUT_JOYSTICK=1   ID_BUS=bluetooth   ID_INPUT_JOYSTICK_INTEGRATION=external
TAGS=:seat:uaccess:power-switch:
open("/dev/input/event7") as phablet: OK
```

What it shows:
- It pairs over classic Bluetooth (bus 0005) and **identifies as an Xbox Wireless Controller** (`045e:02e0`, the Xbox One S Bluetooth ID). The kernel's built-in `hid-microsoft` driver binds it.
- udev tags it as a joystick with `uaccess`. The node is `root:android_input 0660`, and phablet (in `android_input`) can open it. That is what Dolphin's evdev backend and SDL3 need. A confined click app cannot open it (stage 5).
- **Axes:** `ABS=3003f` is X, Y, Z, RX, RY, RZ (two sticks, two analog triggers) plus HAT0X/HAT0Y (the d-pad).
- **Rumble:** `FF=107030000 0` is `FF_RUMBLE`, `FF_PERIODIC`, `FF_SQUARE`, `FF_TRIANGLE`, `FF_SINE` and `FF_GAIN`, so rumble is available.
- **Buttons:** `KEY=3ff000000000000 0 800 0 0` is `BTN_A` … `BTN_TR2` (0x130–0x139, ten codes) plus `KEY_MENU` (0x8b). There are no `BTN_SELECT`, `BTN_START`, `BTN_MODE`, `BTN_THUMBL` or `BTN_THUMBR`. This is how kernels before the Xbox One S Bluetooth button-mapping fix report that controller: the HID button numbers land on generic codes (`BTN_C`, `BTN_Z`, `BTN_TL2`, `BTN_TR2` stand in for View, Menu and the stick clicks). Dolphin's evdev mappings are per-button and user-configurable, and SDL3's controller database knows `045e:02e0`, so this is a default-mapping question for stage 3, not a blocker. Which physical button gives which code was not captured; that needs someone pressing buttons.

## Stage 0 result

All stage 0 questions are answered except the Qt 6 **Widgets** window, which moves to roadmap stage 1 step 5 (it needs a Qt 6 test program built in the stage 1 container, with bundled `libxdg-shell.so`).

| # | Question | Answer |
| --- | --- | --- |
| 1 | Qt 6 on Lomiri's Wayland | Needs a bundled xdg-shell plugin. With it, Qt 6 reaches xdg-shell + Wayland-EGL. Widgets window: stage 1 |
| 1a | Wayland globals | `xdg_wm_base` v1 and core protocols only. None of #14962's optional protocols |
| 1b | Click app `QT_QPA_PLATFORM` | `ubuntumirclient;wayland-egl;xcb` |
| 2 | Toplevel and fullscreen | Windowed 1080x2305 (below the panel); fullscreen 1080x2400. Both shown on screen |
| 3 | GLES on Wayland | Works on screen, including resize and recreate. Pacing 83–87 fps, not locked to the panel (stage 4) |
| 4 | Vulkan on Wayland | Works on screen (`test_vulkan`). `currentExtent` 1x1 needs the stage 2 port fix |
| 5 | Gamepads | A Bluetooth Xbox-type pad works through evdev as phablet, with rumble. Generic button codes (stage 3). USB not tested |
