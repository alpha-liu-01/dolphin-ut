#!/usr/bin/env python3
"""Stage 0 probes for the Dolphin port: Wayland, EGL and Vulkan on Lomiri.

The phone has no compiler and no wayland-info, eglinfo or vulkaninfo, so this
talks to libwayland-client, glvnd's libEGL and the hybris libvulkan directly
through ctypes. Run it as phablet in the Lomiri session:

  XDG_RUNTIME_DIR=/run/user/32011 WAYLAND_DISPLAY=wayland-0 python3 wayland-probe.py MODE

Modes:
  globals            Wayland globals and versions, wl_output modes, wl_seat
                     capabilities. Needs no window.
  egl                eglGetDisplay() on the wl_display (what PR #14962 does),
                     configs, a GLES 3 context on a pbuffer, GL strings, limits
                     and the extensions Dolphin asks for, a shared context on a
                     second thread. Needs no window.
  egl-window         egl, plus an xdg_toplevel with a wl_egl_window: swaps
                     frames (timed), resizes, recreates the surface. Shows a
                     colored window on the screen: the screen must be on and
                     unlocked, or swaps block (a watchdog ends the run).
    --fullscreen       ask for fullscreen before the first commit
    --frames N         frames per timed run (default 240)
  vulkan             Instance and device as Dolphin sees them: extensions,
                     features, limits, subgroup properties, depth formats.
  vulkan-window      vulkan, plus a VK_KHR_wayland_surface on an xdg_toplevel:
                     surface support, capabilities, formats, present modes,
                     a swapchain and one acquire.

Environment: GLIBC_TUNABLES=glibc.rtld.optional_static_tls=65536 is needed for
libhybris inside Python; the script re-executes itself with it when missing.
"""
import ctypes
import faulthandler
import os
import sys
import threading
import time

if "optional_static_tls" not in os.environ.get("GLIBC_TUNABLES", ""):
    os.environ["GLIBC_TUNABLES"] = "glibc.rtld.optional_static_tls=65536"
    os.execv(sys.executable, [sys.executable] + sys.argv)

c_p = ctypes.c_void_p
u32 = ctypes.c_uint32
i32 = ctypes.c_int32
_keep = []  # ctypes objects that C holds pointers to


def keep(x):
    _keep.append(x)
    return x


def loaded(substr):
    with open("/proc/self/maps") as f:
        return sorted({line.split()[-1] for line in f if substr in line and "/" in line})


# --------------------------------------------------------------------------
# Wayland
# --------------------------------------------------------------------------

wl = ctypes.CDLL("libwayland-client.so.0")


class WlMessage(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("signature", ctypes.c_char_p), ("types", ctypes.POINTER(c_p))]


class WlInterface(ctypes.Structure):
    pass


WlInterface._fields_ = [("name", ctypes.c_char_p), ("version", ctypes.c_int),
                        ("method_count", ctypes.c_int), ("methods", ctypes.POINTER(WlMessage)),
                        ("event_count", ctypes.c_int), ("events", ctypes.POINTER(WlMessage))]


class WlArray(ctypes.Structure):
    _fields_ = [("size", ctypes.c_size_t), ("alloc", ctypes.c_size_t), ("data", c_p)]


wl.wl_display_connect.restype = c_p
wl.wl_display_connect.argtypes = [ctypes.c_char_p]
for _f in ("wl_display_roundtrip", "wl_display_dispatch", "wl_display_flush", "wl_display_get_error"):
    getattr(wl, _f).argtypes = [c_p]
wl.wl_display_disconnect.argtypes = [c_p]
wl.wl_proxy_marshal_flags.restype = c_p
wl.wl_proxy_marshal_flags.argtypes = [c_p, u32, c_p, u32, u32]  # variadic after these
wl.wl_proxy_add_listener.argtypes = [c_p, c_p, c_p]
wl.wl_proxy_get_version.restype = u32
wl.wl_proxy_get_version.argtypes = [c_p]
MARSHAL_DESTROY = 1


def core_iface(name):
    return WlInterface.in_dll(wl, name + "_interface")


def iface_addr(iface):
    return ctypes.addressof(iface) if iface is not None else None


def fill_iface(iface, name, version, requests, events):
    def msgs(lst):
        arr = keep((WlMessage * max(1, len(lst)))())
        for i, (n, sig, types) in enumerate(lst):
            t = keep((c_p * max(1, len(types)))(*[iface_addr(x) for x in types]))
            arr[i] = WlMessage(n.encode(), sig.encode(), ctypes.cast(t, ctypes.POINTER(c_p)))
        return arr
    iface.name = keep(name.encode())
    iface.version = version
    iface.method_count = len(requests)
    iface.methods = msgs(requests)
    iface.event_count = len(events)
    iface.events = msgs(events)


# xdg-shell, stable, up to version 5 (from wayland-protocols' xdg-shell.xml).
xdg_wm_base_iface = keep(WlInterface())
xdg_positioner_iface = keep(WlInterface())
xdg_surface_iface = keep(WlInterface())
xdg_toplevel_iface = keep(WlInterface())
xdg_popup_iface = keep(WlInterface())
wl_surface_iface = core_iface("wl_surface")
wl_seat_iface = core_iface("wl_seat")
wl_output_iface = core_iface("wl_output")
fill_iface(xdg_wm_base_iface, "xdg_wm_base", 5,
           [("destroy", "", []), ("create_positioner", "n", [xdg_positioner_iface]),
            ("get_xdg_surface", "no", [xdg_surface_iface, wl_surface_iface]), ("pong", "u", [None])],
           [("ping", "u", [None])])
fill_iface(xdg_positioner_iface, "xdg_positioner", 5, [("destroy", "", [])], [])
fill_iface(xdg_surface_iface, "xdg_surface", 5,
           [("destroy", "", []), ("get_toplevel", "n", [xdg_toplevel_iface]),
            ("get_popup", "n?oo", [xdg_popup_iface, xdg_surface_iface, xdg_positioner_iface]),
            ("set_window_geometry", "iiii", [None] * 4), ("ack_configure", "u", [None])],
           [("configure", "u", [None])])
fill_iface(xdg_toplevel_iface, "xdg_toplevel", 5,
           [("destroy", "", []), ("set_parent", "?o", [xdg_toplevel_iface]), ("set_title", "s", [None]),
            ("set_app_id", "s", [None]), ("show_window_menu", "ouii", [wl_seat_iface, None, None, None]),
            ("move", "ou", [wl_seat_iface, None]), ("resize", "ouu", [wl_seat_iface, None, None]),
            ("set_max_size", "ii", [None, None]), ("set_min_size", "ii", [None, None]),
            ("set_maximized", "", []), ("unset_maximized", "", []),
            ("set_fullscreen", "?o", [wl_output_iface]), ("unset_fullscreen", "", []),
            ("set_minimized", "", [])],
           [("configure", "iia", [None] * 3), ("close", "", []),
            ("configure_bounds", "4ii", [None] * 2), ("wm_capabilities", "5a", [None])])
fill_iface(xdg_popup_iface, "xdg_popup", 5, [("destroy", "", [])], [])

XDG_STATES = {1: "maximized", 2: "fullscreen", 3: "resizing", 4: "activated", 5: "tiled_left",
              6: "tiled_right", 7: "tiled_top", 8: "tiled_bottom", 9: "suspended"}


def array_u32(arr_ptr):
    if not arr_ptr:
        return []
    a = ctypes.cast(arr_ptr, ctypes.POINTER(WlArray)).contents
    n = a.size // 4
    return list((u32 * n).from_address(a.data)) if n else []


def listen(proxy, *callbacks):
    table = keep((c_p * len(callbacks))(*[ctypes.cast(keep(cb), c_p) for cb in callbacks]))
    wl.wl_proxy_add_listener(proxy, table, None)


def cb(*argtypes):
    return ctypes.CFUNCTYPE(None, c_p, c_p, *argtypes)


class Wayland:
    def __init__(self):
        self.display = wl.wl_display_connect(None)
        if not self.display:
            raise SystemExit("wl_display_connect failed: is WAYLAND_DISPLAY/XDG_RUNTIME_DIR set?")
        self.globals = {}  # interface -> (name, version)
        self.outputs = []
        self.seat_caps = None
        self.seat_name = None
        registry = wl.wl_proxy_marshal_flags(self.display, 1, iface_addr(core_iface("wl_registry")),
                                             wl.wl_proxy_get_version(self.display), 0, c_p(None))
        self.registry = registry

        def on_global(data, reg, name, interface, version):
            self.globals.setdefault(interface.decode(), []).append((name, version))

        listen(registry, cb(u32, ctypes.c_char_p, u32)(on_global), cb(u32)(lambda d, r, n: None))
        self.roundtrip()

    def roundtrip(self):
        if wl.wl_display_roundtrip(self.display) < 0:
            raise SystemExit("wl_display_roundtrip failed, error %d" % wl.wl_display_get_error(self.display))

    def bind(self, interface, iface_struct, max_version):
        if interface not in self.globals:
            return None, 0
        name, version = self.globals[interface][0]
        v = min(version, max_version)
        p = wl.wl_proxy_marshal_flags(self.registry, 0, iface_addr(iface_struct), v, 0,
                                      u32(name), ctypes.c_char_p(iface_struct.name), u32(v), c_p(None))
        return p, v

    def probe_outputs_and_seat(self):
        for name, version in self.globals.get("wl_output", []):
            v = min(version, 4)
            p = wl.wl_proxy_marshal_flags(self.registry, 0, iface_addr(wl_output_iface), v, 0,
                                          u32(name), ctypes.c_char_p(b"wl_output"), u32(v), c_p(None))
            info = {"version": v, "modes": []}
            self.outputs.append(info)

            def geometry(d, o, x, y, pw, ph, sub, make, model, transform, info=info):
                info.update(physical_mm=(pw, ph), make=(make or b"").decode(),
                            model=(model or b"").decode(), transform=transform)

            def mode(d, o, flags, w, h, refresh, info=info):
                info["modes"].append((w, h, refresh / 1000.0, "current" if flags & 1 else "",
                                      "preferred" if flags & 2 else ""))

            def scale(d, o, s, info=info):
                info["scale"] = s

            def name_ev(d, o, s, info=info):
                info["name"] = (s or b"").decode()

            def desc_ev(d, o, s, info=info):
                info["description"] = (s or b"").decode()

            listen(p, cb(i32, i32, i32, i32, i32, ctypes.c_char_p, ctypes.c_char_p, i32)(geometry),
                   cb(u32, i32, i32, i32)(mode), cb()(lambda d, o: None), cb(i32)(scale),
                   cb(ctypes.c_char_p)(name_ev), cb(ctypes.c_char_p)(desc_ev))
        seat, v = self.bind("wl_seat", wl_seat_iface, 5)
        if seat:
            def caps(d, s, c):
                self.seat_caps = c

            def sname(d, s, n):
                self.seat_name = (n or b"").decode()
            listen(seat, cb(u32)(caps), cb(ctypes.c_char_p)(sname))
        self.roundtrip()
        self.roundtrip()


class Toplevel:
    """A wl_surface with an xdg_toplevel. Records every configure."""

    def __init__(self, w, fullscreen=False, title=b"dolphin-ut stage 0 probe"):
        self.w = w
        self.compositor, _ = w.bind("wl_compositor", core_iface("wl_compositor"), 4)
        self.wm_base, self.wm_version = w.bind("xdg_wm_base", xdg_wm_base_iface, 5)
        if not self.compositor or not self.wm_base:
            raise SystemExit("no wl_compositor or xdg_wm_base")
        listen(self.wm_base, cb(u32)(lambda d, b, serial: wl.wl_proxy_marshal_flags(
            self.wm_base, 3, None, self.wm_version, 0, u32(serial))))
        self.surface = wl.wl_proxy_marshal_flags(self.compositor, 0, iface_addr(wl_surface_iface),
                                                 wl.wl_proxy_get_version(self.compositor), 0, c_p(None))
        self.xdg_surface = wl.wl_proxy_marshal_flags(self.wm_base, 2, iface_addr(xdg_surface_iface),
                                                     self.wm_version, 0, c_p(None), c_p(self.surface))
        self.toplevel = wl.wl_proxy_marshal_flags(self.xdg_surface, 1, iface_addr(xdg_toplevel_iface),
                                                  self.wm_version, 0, c_p(None))
        self.configures = []  # (time, width, height, states)
        self.pending = None
        self.serials = []
        self.closed = False
        self.bounds = None
        self.wm_caps = None

        def tl_configure(d, t, width, height, states):
            self.pending = (width, height, [XDG_STATES.get(s, s) for s in array_u32(states)])

        def tl_close(d, t):
            self.closed = True

        def tl_bounds(d, t, bw, bh):
            self.bounds = (bw, bh)

        def tl_caps(d, t, caps):
            self.wm_caps = array_u32(caps)

        def xs_configure(d, s, serial):
            self.serials.append(serial)
            wl.wl_proxy_marshal_flags(self.xdg_surface, 4, None, self.wm_version, 0, u32(serial))
            if self.pending is not None:
                self.configures.append((time.monotonic(),) + self.pending)

        listen(self.toplevel, cb(i32, i32, c_p)(tl_configure), cb()(tl_close),
               cb(i32, i32)(tl_bounds), cb(c_p)(tl_caps))
        listen(self.xdg_surface, cb(u32)(xs_configure))
        wl.wl_proxy_marshal_flags(self.toplevel, 2, None, self.wm_version, 0, ctypes.c_char_p(title))
        wl.wl_proxy_marshal_flags(self.toplevel, 3, None, self.wm_version, 0, ctypes.c_char_p(b"dolphin-ut-probe"))
        if fullscreen:
            wl.wl_proxy_marshal_flags(self.toplevel, 11, None, self.wm_version, 0, c_p(None))
        self.commit()
        deadline = time.monotonic() + 5
        while not self.serials and time.monotonic() < deadline:
            w.roundtrip()
            time.sleep(0.05)

    def commit(self):
        wl.wl_proxy_marshal_flags(self.surface, 6, None, wl.wl_proxy_get_version(self.surface), 0)
        wl.wl_display_flush(self.w.display)

    def size(self, default=(1080, 2400)):
        for c in reversed(self.configures):
            if c[1] > 0 and c[2] > 0:
                return c[1], c[2]
        return default

    def report(self):
        print("xdg_wm_base version     %d" % self.wm_version)
        print("configure events        %d (serials %s)" % (len(self.serials), self.serials[:8]))
        for t, cw, ch, states in self.configures[:8]:
            print("  toplevel configure    %dx%d states=%s" % (cw, ch, states))
        if self.bounds:
            print("configure_bounds        %dx%d" % self.bounds)
        if self.wm_caps is not None:
            print("wm_capabilities         %s" % self.wm_caps)
        if self.closed:
            print("close event received")


def mode_globals():
    w = Wayland()
    w.probe_outputs_and_seat()
    print("Wayland globals on %s:" % os.environ.get("WAYLAND_DISPLAY", "wayland-0"))
    for iface in sorted(w.globals):
        print("  %-44s v%s" % (iface, ",".join(str(v) for _, v in w.globals[iface])))
    for i, o in enumerate(w.outputs):
        print("wl_output %d: %s" % (i, {k: v for k, v in o.items() if k != "modes"}))
        for m in o["modes"]:
            print("  mode %dx%d @ %.2f Hz %s %s" % m)
    if w.seat_caps is not None:
        caps = [n for b, n in ((1, "pointer"), (2, "keyboard"), (4, "touch")) if w.seat_caps & b]
        print("wl_seat '%s' capabilities: %s" % (w.seat_name, caps))
    wanted = ["xdg_wm_base", "wl_subcompositor", "wl_shm", "zwp_linux_dmabuf_v1", "wp_viewporter",
              "wp_content_type_manager_v1", "zwp_pointer_constraints_v1",
              "zwp_relative_pointer_manager_v1", "wp_pointer_warp_v1", "zxdg_decoration_manager_v1",
              "wp_fractional_scale_manager_v1", "wp_presentation", "zxdg_output_manager_v1",
              "zwp_text_input_manager_v3", "zwp_text_input_manager_v2", "zwp_idle_inhibit_manager_v1",
              "xdg_activation_v1", "wp_tearing_control_manager_v1", "zxdg_shell_v6", "wl_shell"]
    print("Checks:")
    for name in wanted:
        print("  %-44s %s" % (name, "yes v%d" % w.globals[name][0][1] if name in w.globals else "no"))
    return 0


# --------------------------------------------------------------------------
# EGL / GLES
# --------------------------------------------------------------------------

DOLPHIN_GL_EXTENSIONS = """
GL_ANDROID_extension_pack_es31a GL_ARM_shader_framebuffer_fetch GL_EXT_blend_func_extended
GL_EXT_buffer_storage GL_EXT_copy_image GL_EXT_draw_elements_base_vertex GL_EXT_geometry_point_size
GL_EXT_shader_framebuffer_fetch GL_EXT_shader_pixel_local_storage GL_EXT_texture_buffer
GL_EXT_texture_compression_s3tc GL_EXT_texture_filter_anisotropic GL_KHR_debug GL_KHR_shader_subgroup
GL_NV_copy_image GL_NV_occlusion_query_samples GL_NV_primitive_restart GL_OES_copy_image
GL_OES_draw_elements_base_vertex GL_OES_geometry_point_size GL_OES_texture_buffer
GL_OES_texture_storage_multisample_2d_array GL_EXT_clip_cull_distance GL_EXT_geometry_shader
GL_OES_geometry_shader GL_EXT_texture_border_clamp GL_EXT_primitive_bounding_box
GL_EXT_shader_io_blocks GL_OES_get_program_binary GL_EXT_texture_compression_bptc
GL_EXT_clip_control GL_EXT_depth_clamp GL_EXT_disjoint_timer_query GL_KHR_texture_compression_astc_ldr
GL_EXT_multisampled_render_to_texture GL_OES_sample_shading GL_OES_shader_multisample_interpolation
GL_EXT_texture_format_BGRA8888 GL_EXT_color_buffer_float GL_EXT_color_buffer_half_float
""".split()

GL_LIMITS = [("GL_MAJOR_VERSION", 0x821B), ("GL_MINOR_VERSION", 0x821C), ("GL_MAX_TEXTURE_SIZE", 0x0D33),
             ("GL_MAX_ARRAY_TEXTURE_LAYERS", 0x88FF), ("GL_MAX_SAMPLES", 0x8D57),
             ("GL_MAX_COLOR_TEXTURE_SAMPLES", 0x910E), ("GL_MAX_DEPTH_TEXTURE_SAMPLES", 0x910F),
             ("GL_MAX_UNIFORM_BLOCK_SIZE", 0x8A30), ("GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT", 0x8A34),
             ("GL_MAX_SHADER_STORAGE_BLOCK_SIZE", 0x90DE), ("GL_MAX_FRAGMENT_SHADER_STORAGE_BLOCKS", 0x90DA),
             ("GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS", 0x90D6), ("GL_MAX_TEXTURE_BUFFER_SIZE", 0x8C2B),
             ("GL_MAX_COMPUTE_SHARED_MEMORY_SIZE", 0x8262), ("GL_MAX_GEOMETRY_OUTPUT_VERTICES", 0x8DE0),
             ("GL_MAX_CLIP_DISTANCES", 0x0D32), ("GL_NUM_PROGRAM_BINARY_FORMATS", 0x87FE),
             ("GL_MAX_DRAW_BUFFERS", 0x8824), ("GL_MAX_DUAL_SOURCE_DRAW_BUFFERS", 0x88FC)]

EGL_NONE = 0x3038


class Egl:
    def __init__(self, native_display):
        egl = self.egl = ctypes.CDLL("libEGL.so.1")
        for name, res, args in (
                ("eglGetDisplay", c_p, [c_p]), ("eglQueryString", ctypes.c_char_p, [c_p, ctypes.c_int]),
                ("eglInitialize", ctypes.c_uint, [c_p, c_p, c_p]), ("eglBindAPI", ctypes.c_uint, [ctypes.c_uint]),
                ("eglChooseConfig", ctypes.c_uint, [c_p, c_p, c_p, ctypes.c_int, c_p]),
                ("eglGetConfigAttrib", ctypes.c_uint, [c_p, c_p, ctypes.c_int, c_p]),
                ("eglCreatePbufferSurface", c_p, [c_p, c_p, c_p]),
                ("eglCreateWindowSurface", c_p, [c_p, c_p, c_p, c_p]),
                ("eglDestroySurface", ctypes.c_uint, [c_p, c_p]),
                ("eglCreateContext", c_p, [c_p, c_p, c_p, c_p]),
                ("eglDestroyContext", ctypes.c_uint, [c_p, c_p]),
                ("eglMakeCurrent", ctypes.c_uint, [c_p, c_p, c_p, c_p]),
                ("eglSwapBuffers", ctypes.c_uint, [c_p, c_p]), ("eglSwapInterval", ctypes.c_uint, [c_p, ctypes.c_int]),
                ("eglQuerySurface", ctypes.c_uint, [c_p, c_p, ctypes.c_int, c_p]),
                ("eglGetError", ctypes.c_int, []), ("eglGetProcAddress", c_p, [ctypes.c_char_p]),
                ("eglTerminate", ctypes.c_uint, [c_p])):
            f = getattr(egl, name)
            f.restype, f.argtypes = res, args
        self.client_ext = (egl.eglQueryString(None, 0x3055) or b"").decode().split()
        self.d = egl.eglGetDisplay(native_display)
        maj, mnr = ctypes.c_int(), ctypes.c_int()
        self.ok = bool(self.d) and bool(egl.eglInitialize(self.d, ctypes.byref(maj), ctypes.byref(mnr)))
        self.version = (maj.value, mnr.value)

    def err(self):
        return "0x%x" % self.egl.eglGetError()

    def q(self, k):
        return (self.egl.eglQueryString(self.d, k) or b"").decode()

    def config(self, surface_bit):
        # RGBA8888, depth 0 (Dolphin renders to its own FBOs), GLES 3.
        attrs = (ctypes.c_int * 17)(0x3033, surface_bit, 0x3040, 0x40, 0x3024, 8, 0x3023, 8, 0x3022, 8,
                                    0x3021, 8, 0x3025, 0, EGL_NONE, 0, 0)
        cfg = c_p()
        n = ctypes.c_int()
        if not self.egl.eglChooseConfig(self.d, attrs, ctypes.byref(cfg), 1, ctypes.byref(n)) or n.value < 1:
            return None
        return cfg

    def count_configs(self, surface_bit):
        attrs = (ctypes.c_int * 5)(0x3033, surface_bit, 0x3040, 0x40, EGL_NONE)
        n = ctypes.c_int()
        self.egl.eglChooseConfig(self.d, attrs, None, 0, ctypes.byref(n))
        return n.value

    def context(self, cfg, share=None):
        self.egl.eglBindAPI(0x30A0)
        return self.egl.eglCreateContext(self.d, cfg, share, (ctypes.c_int * 3)(0x3098, 3, EGL_NONE))

    def pbuffer(self, cfg, w=16, h=16):
        return self.egl.eglCreatePbufferSurface(self.d, cfg, (ctypes.c_int * 5)(0x3057, w, 0x3056, h, EGL_NONE))

    def surface_size(self, s):
        w, h = ctypes.c_int(), ctypes.c_int()
        self.egl.eglQuerySurface(self.d, s, 0x3057, ctypes.byref(w))
        self.egl.eglQuerySurface(self.d, s, 0x3056, ctypes.byref(h))
        return w.value, h.value

    def gl(self, name, restype, *argtypes):
        addr = self.egl.eglGetProcAddress(name.encode())
        return ctypes.CFUNCTYPE(restype, *argtypes)(addr) if addr else None


def gl_report(e):
    glGetString = e.gl("glGetString", ctypes.c_char_p, ctypes.c_uint)
    glGetStringi = e.gl("glGetStringi", ctypes.c_char_p, ctypes.c_uint, ctypes.c_uint)
    glGetIntegerv = e.gl("glGetIntegerv", None, ctypes.c_uint, c_p)
    for name, k in (("GL_VENDOR", 0x1F00), ("GL_RENDERER", 0x1F01), ("GL_VERSION", 0x1F02),
                    ("GL_SHADING_LANGUAGE_VERSION", 0x8B8C)):
        print("%-36s %s" % (name, (glGetString(k) or b"").decode()))
    n = ctypes.c_int()
    glGetIntegerv(0x821D, ctypes.byref(n))
    exts = sorted((glGetStringi(0x1F03, i) or b"").decode() for i in range(n.value))
    print("%-36s %d" % ("GL_NUM_EXTENSIONS", n.value))
    for name, k in GL_LIMITS:
        v = (ctypes.c_int * 4)()
        glGetIntegerv(k, v)
        print("%-36s %d" % (name, v[0]))
    print("Extensions Dolphin checks for (GLES-relevant):")
    have = set(exts)
    print("  present: " + " ".join(x for x in DOLPHIN_GL_EXTENSIONS if x in have))
    print("  absent:  " + " ".join(x for x in DOLPHIN_GL_EXTENSIONS if x not in have))
    print("All GL extensions: " + " ".join(exts))


def mode_egl(window=False, fullscreen=False, frames=240):
    w = Wayland()
    e = Egl(c_p(w.display))
    print("EGL client extensions  %s" % " ".join(e.client_ext))
    if not e.ok:
        print("eglGetDisplay(wl_display)/eglInitialize failed, error %s" % e.err())
        return 1
    print("eglGetDisplay(wl_display) + eglInitialize: OK, EGL %d.%d" % e.version)
    for name, k in (("EGL_VENDOR", 0x3053), ("EGL_VERSION", 0x3054), ("EGL_CLIENT_APIS", 0x308D)):
        print("%-22s %s" % (name, e.q(k)))
    eglext = e.q(0x3055).split()
    print("EGL_EXTENSIONS (%d)    %s" % (len(eglext), " ".join(eglext)))
    print("libhybris EGL platforms loaded: %s" % loaded("eglplatform_"))
    print("EGL vendor libraries loaded: %s" % loaded("libEGL_"))
    print("GLES3 configs: window %d, pbuffer %d" % (e.count_configs(0x4), e.count_configs(0x1)))
    print("EGL_KHR_surfaceless_context: %s" % ("EGL_KHR_surfaceless_context" in eglext))

    pcfg = e.config(0x1)
    if not pcfg:
        print("no GLES3 pbuffer config, error %s" % e.err())
        return 1
    ctx = e.context(pcfg)
    pb = e.pbuffer(pcfg)
    if not ctx or not pb or not e.egl.eglMakeCurrent(e.d, pb, pb, ctx):
        print("GLES3 context on a pbuffer failed, error %s" % e.err())
        return 1
    print("GLES3 context on a pbuffer: OK")
    gl_report(e)

    # Dolphin creates shared contexts for its shader compile threads.
    result = {}

    def worker():
        sctx = e.context(pcfg, share=ctx)
        spb = e.pbuffer(pcfg)
        ok = bool(sctx and spb and e.egl.eglMakeCurrent(e.d, spb, spb, sctx))
        sl = bool(sctx and e.egl.eglMakeCurrent(e.d, None, None, sctx)) if ok else False
        result.update(shared=bool(sctx), pbuffer=ok, surfaceless=sl, err=e.err())
        e.egl.eglMakeCurrent(e.d, None, None, None)
        if spb:
            e.egl.eglDestroySurface(e.d, spb)
        if sctx:
            e.egl.eglDestroyContext(e.d, sctx)
    t = threading.Thread(target=worker)
    t.start()
    t.join()
    print("shared context on a second thread: created=%(shared)s, current on pbuffer=%(pbuffer)s, "
          "current surfaceless=%(surfaceless)s (last error %(err)s)" % result)

    if window:
        rc = egl_window(w, e, ctx, fullscreen, frames)
        if rc:
            return rc
    e.egl.eglMakeCurrent(e.d, None, None, None)
    e.egl.eglTerminate(e.d)
    return 0


def egl_window(w, e, ctx, fullscreen, frames):
    wegl = ctypes.CDLL("libwayland-egl.so.1")
    wegl.wl_egl_window_create.restype = c_p
    wegl.wl_egl_window_create.argtypes = [c_p, ctypes.c_int, ctypes.c_int]
    wegl.wl_egl_window_resize.argtypes = [c_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int]
    wegl.wl_egl_window_destroy.argtypes = [c_p]
    top = Toplevel(w, fullscreen=fullscreen)
    top.report()
    width, height = top.size()
    print("window size used        %dx%d" % (width, height))
    wcfg = e.config(0x4)
    if not wcfg:
        print("no GLES3 window config, error %s" % e.err())
        return 1
    # The window needs a context made from a window config; Dolphin uses one config for both.
    wctx = e.context(wcfg, share=ctx)
    glClearColor = e.gl("glClearColor", None, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float)
    glClear = e.gl("glClear", None, ctypes.c_uint)
    glViewport = e.gl("glViewport", None, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)

    def make(wd, ht):
        ew = wegl.wl_egl_window_create(c_p(top.surface), wd, ht)
        es = e.egl.eglCreateWindowSurface(e.d, wcfg, c_p(ew), None)
        return ew, es

    def run(es, n, interval, label):
        e.egl.eglSwapInterval(e.d, interval)
        sw, sh = e.surface_size(es)
        glViewport(0, 0, sw, sh)
        t0 = time.monotonic()
        bad = 0
        for i in range(n):
            glClearColor(0.0, 0.3 + 0.5 * ((i // 30) % 2), 0.6, 1.0)
            glClear(0x4000)
            if not e.egl.eglSwapBuffers(e.d, es):
                bad += 1
            w.roundtrip() if i % 30 == 0 else None
        dt = time.monotonic() - t0
        print("%-24s %d frames in %.2f s = %.1f fps (swap interval %d, surface %dx%d, failed swaps %d)"
              % (label, n, dt, n / dt, interval, sw, sh, bad))

    faulthandler.dump_traceback_later(60, exit=True)  # swaps block if the screen is off
    ew, es = make(width, height)
    if not es or not e.egl.eglMakeCurrent(e.d, es, es, wctx):
        print("eglCreateWindowSurface/MakeCurrent on the wl_egl_window failed, error %s" % e.err())
        return 1
    print("libhybris EGL platforms loaded: %s" % loaded("eglplatform_"))
    run(es, frames, 1, "vsync run")
    run(es, frames, 0, "no-vsync run")
    # Resize: Dolphin calls wl_egl_window_resize from UpdateSurface().
    wegl.wl_egl_window_resize(c_p(ew), width // 2, height // 2, 0, 0)
    run(es, 30, 1, "after resize to half")
    wegl.wl_egl_window_resize(c_p(ew), width, height, 0, 0)
    run(es, 30, 1, "after resize back")
    # Recreate: what Dolphin does on a surface change.
    e.egl.eglMakeCurrent(e.d, None, None, None)
    e.egl.eglDestroySurface(e.d, es)
    wegl.wl_egl_window_destroy(c_p(ew))
    ew, es = make(width, height)
    ok = bool(es) and bool(e.egl.eglMakeCurrent(e.d, es, es, wctx))
    print("surface recreate        %s" % ("OK" if ok else "failed, error %s" % e.err()))
    if ok:
        run(es, 60, 1, "after recreate")
    w.roundtrip()
    top.report()
    faulthandler.cancel_dump_traceback_later()
    e.egl.eglMakeCurrent(e.d, None, None, None)
    e.egl.eglDestroySurface(e.d, es)
    wegl.wl_egl_window_destroy(c_p(ew))
    return 0


# --------------------------------------------------------------------------
# Vulkan
# --------------------------------------------------------------------------

FEATURES = ("robustBufferAccess fullDrawIndexUint32 imageCubeArray independentBlend geometryShader "
            "tessellationShader sampleRateShading dualSrcBlend logicOp multiDrawIndirect "
            "drawIndirectFirstInstance depthClamp depthBiasClamp fillModeNonSolid depthBounds wideLines "
            "largePoints alphaToOne multiViewport samplerAnisotropy textureCompressionETC2 "
            "textureCompressionASTC_LDR textureCompressionBC occlusionQueryPrecise pipelineStatisticsQuery "
            "vertexPipelineStoresAndAtomics fragmentStoresAndAtomics shaderTessellationAndGeometryPointSize "
            "shaderImageGatherExtended shaderStorageImageExtendedFormats shaderStorageImageMultisample "
            "shaderStorageImageReadWithoutFormat shaderStorageImageWriteWithoutFormat "
            "shaderUniformBufferArrayDynamicIndexing shaderSampledImageArrayDynamicIndexing "
            "shaderStorageBufferArrayDynamicIndexing shaderStorageImageArrayDynamicIndexing shaderClipDistance "
            "shaderCullDistance shaderFloat64 shaderInt64 shaderInt16 shaderResourceResidency "
            "shaderResourceMinLod sparseBinding sparseResidencyBuffer sparseResidencyImage2D "
            "sparseResidencyImage3D sparseResidency2Samples sparseResidency4Samples sparseResidency8Samples "
            "sparseResidency16Samples sparseResidencyAliased variableMultisampleRate inheritedQueries").split()
DOLPHIN_FEATURES = ("depthClamp dualSrcBlend fragmentStoresAndAtomics geometryShader largePoints logicOp "
                    "occlusionQueryPrecise samplerAnisotropy sampleRateShading shaderClipDistance "
                    "shaderStorageImageMultisample shaderTessellationAndGeometryPointSize "
                    "textureCompressionBC").split()
DOLPHIN_DEVICE_EXTS = ("VK_KHR_swapchain VK_EXT_depth_range_unrestricted VK_EXT_memory_budget "
                       "VK_EXT_depth_clamp_control VK_KHR_driver_properties VK_KHR_shader_draw_parameters "
                       "VK_EXT_custom_border_color VK_KHR_dedicated_allocation VK_KHR_get_memory_requirements2 "
                       "VK_KHR_maintenance1 VK_KHR_maintenance2 VK_KHR_maintenance3").split()


class VkFeatures(ctypes.Structure):
    _fields_ = [(n, u32) for n in FEATURES]


_u64 = ctypes.c_uint64
_f = ctypes.c_float


class VkLimits(ctypes.Structure):
    _fields_ = [(n, u32) for n in (
        "maxImageDimension1D maxImageDimension2D maxImageDimension3D maxImageDimensionCube maxImageArrayLayers "
        "maxTexelBufferElements maxUniformBufferRange maxStorageBufferRange maxPushConstantsSize "
        "maxMemoryAllocationCount maxSamplerAllocationCount").split()] + [
        ("bufferImageGranularity", _u64), ("sparseAddressSpaceSize", _u64)] + [(n, u32) for n in (
        "maxBoundDescriptorSets maxPerStageDescriptorSamplers maxPerStageDescriptorUniformBuffers "
        "maxPerStageDescriptorStorageBuffers maxPerStageDescriptorSampledImages "
        "maxPerStageDescriptorStorageImages maxPerStageDescriptorInputAttachments maxPerStageResources "
        "maxDescriptorSetSamplers maxDescriptorSetUniformBuffers maxDescriptorSetUniformBuffersDynamic "
        "maxDescriptorSetStorageBuffers maxDescriptorSetStorageBuffersDynamic maxDescriptorSetSampledImages "
        "maxDescriptorSetStorageImages maxDescriptorSetInputAttachments maxVertexInputAttributes "
        "maxVertexInputBindings maxVertexInputAttributeOffset maxVertexInputBindingStride "
        "maxVertexOutputComponents maxTessellationGenerationLevel maxTessellationPatchSize "
        "maxTessellationControlPerVertexInputComponents maxTessellationControlPerVertexOutputComponents "
        "maxTessellationControlPerPatchOutputComponents maxTessellationControlTotalOutputComponents "
        "maxTessellationEvaluationInputComponents maxTessellationEvaluationOutputComponents "
        "maxGeometryShaderInvocations maxGeometryInputComponents maxGeometryOutputComponents "
        "maxGeometryOutputVertices maxGeometryTotalOutputComponents maxFragmentInputComponents "
        "maxFragmentOutputAttachments maxFragmentDualSrcAttachments maxFragmentCombinedOutputResources "
        "maxComputeSharedMemorySize").split()] + [
        ("maxComputeWorkGroupCount", u32 * 3), ("maxComputeWorkGroupInvocations", u32),
        ("maxComputeWorkGroupSize", u32 * 3)] + [(n, u32) for n in (
        "subPixelPrecisionBits subTexelPrecisionBits mipmapPrecisionBits maxDrawIndexedIndexValue "
        "maxDrawIndirectCount").split()] + [
        ("maxSamplerLodBias", _f), ("maxSamplerAnisotropy", _f), ("maxViewports", u32),
        ("maxViewportDimensions", u32 * 2), ("viewportBoundsRange", _f * 2), ("viewportSubPixelBits", u32),
        ("minMemoryMapAlignment", ctypes.c_size_t), ("minTexelBufferOffsetAlignment", _u64),
        ("minUniformBufferOffsetAlignment", _u64), ("minStorageBufferOffsetAlignment", _u64),
        ("minTexelOffset", i32), ("maxTexelOffset", u32), ("minTexelGatherOffset", i32),
        ("maxTexelGatherOffset", u32), ("minInterpolationOffset", _f), ("maxInterpolationOffset", _f)] + [
        (n, u32) for n in (
            "subPixelInterpolationOffsetBits maxFramebufferWidth maxFramebufferHeight maxFramebufferLayers "
            "framebufferColorSampleCounts framebufferDepthSampleCounts framebufferStencilSampleCounts "
            "framebufferNoAttachmentsSampleCounts maxColorAttachments sampledImageColorSampleCounts "
            "sampledImageIntegerSampleCounts sampledImageDepthSampleCounts sampledImageStencilSampleCounts "
            "storageImageSampleCounts maxSampleMaskWords timestampComputeAndGraphics").split()] + [
        ("timestampPeriod", _f)] + [(n, u32) for n in (
            "maxClipDistances maxCullDistances maxCombinedClipAndCullDistances discreteQueuePriorities").split()] + [
        ("pointSizeRange", _f * 2), ("lineWidthRange", _f * 2), ("pointSizeGranularity", _f),
        ("lineWidthGranularity", _f), ("strictLines", u32), ("standardSampleLocations", u32),
        ("optimalBufferCopyOffsetAlignment", _u64), ("optimalBufferCopyRowPitchAlignment", _u64),
        ("nonCoherentAtomSize", _u64)]


class VkProps(ctypes.Structure):
    _fields_ = [("apiVersion", u32), ("driverVersion", u32), ("vendorID", u32), ("deviceID", u32),
                ("deviceType", ctypes.c_int), ("deviceName", ctypes.c_char * 256),
                ("pipelineCacheUUID", ctypes.c_ubyte * 16), ("limits", VkLimits), ("sparse", u32 * 5)]


class VkProps2(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("properties", VkProps)]


class VkSubgroupProps(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("subgroupSize", u32), ("supportedStages", u32),
                ("supportedOperations", u32), ("quadOperationsInAllStages", u32)]


class VkDriverProps(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("driverID", ctypes.c_int),
                ("driverName", ctypes.c_char * 256), ("driverInfo", ctypes.c_char * 256),
                ("conformanceVersion", ctypes.c_ubyte * 4)]


class VkExt(ctypes.Structure):
    _fields_ = [("extensionName", ctypes.c_char * 256), ("specVersion", u32)]


class VkAppInfo(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("pApplicationName", ctypes.c_char_p),
                ("applicationVersion", u32), ("pEngineName", ctypes.c_char_p), ("engineVersion", u32),
                ("apiVersion", u32)]


class VkInstanceCI(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32), ("pApplicationInfo", ctypes.POINTER(VkAppInfo)),
                ("enabledLayerCount", u32), ("ppEnabledLayerNames", c_p), ("enabledExtensionCount", u32),
                ("ppEnabledExtensionNames", c_p)]


class VkQueueFamily(ctypes.Structure):
    _fields_ = [("queueFlags", u32), ("queueCount", u32), ("timestampValidBits", u32), ("minImageTransferGranularity", u32 * 3)]


class VkFormatProps(ctypes.Structure):
    _fields_ = [("linear", u32), ("optimal", u32), ("buffer", u32)]


class VkWaylandSurfaceCI(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32), ("display", c_p), ("surface", c_p)]


class VkExtent2D(ctypes.Structure):
    _fields_ = [("width", u32), ("height", u32)]


class VkSurfaceCaps(ctypes.Structure):
    _fields_ = [("minImageCount", u32), ("maxImageCount", u32), ("currentExtent", VkExtent2D),
                ("minImageExtent", VkExtent2D), ("maxImageExtent", VkExtent2D), ("maxImageArrayLayers", u32),
                ("supportedTransforms", u32), ("currentTransform", u32), ("supportedCompositeAlpha", u32),
                ("supportedUsageFlags", u32)]


class VkSurfaceFormat(ctypes.Structure):
    _fields_ = [("format", ctypes.c_int), ("colorSpace", ctypes.c_int)]


class VkQueueCI(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32), ("queueFamilyIndex", u32),
                ("queueCount", u32), ("pQueuePriorities", ctypes.POINTER(ctypes.c_float))]


class VkDeviceCI(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32), ("queueCreateInfoCount", u32),
                ("pQueueCreateInfos", ctypes.POINTER(VkQueueCI)), ("enabledLayerCount", u32),
                ("ppEnabledLayerNames", c_p), ("enabledExtensionCount", u32), ("ppEnabledExtensionNames", c_p),
                ("pEnabledFeatures", c_p)]


class VkSwapchainCI(ctypes.Structure):
    _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32), ("surface", _u64), ("minImageCount", u32),
                ("imageFormat", ctypes.c_int), ("imageColorSpace", ctypes.c_int), ("imageExtent", VkExtent2D),
                ("imageArrayLayers", u32), ("imageUsage", u32), ("imageSharingMode", ctypes.c_int),
                ("queueFamilyIndexCount", u32), ("pQueueFamilyIndices", c_p), ("preTransform", u32),
                ("compositeAlpha", u32), ("presentMode", ctypes.c_int), ("clipped", u32), ("oldSwapchain", _u64)]


def ver(v):
    return "%d.%d.%d" % (v >> 22, (v >> 12) & 0x3ff, v & 0xfff)


def enum_exts(vk, pd=None):
    n = u32()
    if pd is None:
        vk.vkEnumerateInstanceExtensionProperties(None, ctypes.byref(n), None)
        arr = (VkExt * n.value)()
        vk.vkEnumerateInstanceExtensionProperties(None, ctypes.byref(n), arr)
    else:
        vk.vkEnumerateDeviceExtensionProperties(pd, None, ctypes.byref(n), None)
        arr = (VkExt * n.value)()
        vk.vkEnumerateDeviceExtensionProperties(pd, None, ctypes.byref(n), arr)
    return sorted(e.extensionName.decode() for e in arr)


def strarr(names):
    return keep((ctypes.c_char_p * max(1, len(names)))(*[n.encode() for n in names]))


PRESENT_MODES = {0: "IMMEDIATE", 1: "MAILBOX", 2: "FIFO", 3: "FIFO_RELAXED"}
FORMATS = {37: "R8G8B8A8_UNORM", 43: "R8G8B8A8_SRGB", 44: "B8G8R8A8_UNORM", 50: "B8G8R8A8_SRGB",
           64: "A2B10G10R10_UNORM_PACK32", 58: "A2R10G10B10_UNORM_PACK32", 4: "R5G6B5_UNORM_PACK16",
           97: "R16G16B16A16_SFLOAT"}
DEPTH_FORMATS = [(124, "D16_UNORM"), (126, "D32_SFLOAT"), (129, "D24_UNORM_S8_UINT"), (130, "D32_SFLOAT_S8_UINT"),
                 (37, "R8G8B8A8_UNORM"), (44, "B8G8R8A8_UNORM"), (64, "A2B10G10R10_UNORM_PACK32"),
                 (97, "R16G16B16A16_SFLOAT"), (100, "R32_SFLOAT"), (109, "R32G32B32A32_SFLOAT"),
                 (131, "BC1_RGB_UNORM_BLOCK"), (147, "ETC2_R8G8B8_UNORM_BLOCK"), (157, "ASTC_4x4_UNORM_BLOCK")]


def mode_vulkan(window=False):
    vk = ctypes.CDLL("libvulkan.so.1")
    vk.vkGetInstanceProcAddr.restype = c_p
    vk.vkGetInstanceProcAddr.argtypes = [c_p, ctypes.c_char_p]
    iexts = enum_exts(vk)
    print("libvulkan: %s" % loaded("libvulkan"))
    print("instance extensions (%d): %s" % (len(iexts), " ".join(iexts)))
    want = ["VK_KHR_surface", "VK_KHR_wayland_surface", "VK_KHR_get_physical_device_properties2",
            "VK_KHR_get_surface_capabilities2", "VK_EXT_debug_utils", "VK_KHR_xlib_surface"]
    print("instance extensions Dolphin uses: " + ", ".join("%s=%s" % (x, x in iexts) for x in want))
    enable = [x for x in want[:5] if x in iexts]
    app = VkAppInfo(0, None, b"dolphin-ut-probe", 1, b"none", 1, (1 << 22) | (1 << 12))
    ci = VkInstanceCI(1, None, 0, ctypes.pointer(app), 0, None, len(enable), ctypes.cast(strarr(enable), c_p))
    inst = c_p()
    r = vk.vkCreateInstance(ctypes.byref(ci), None, ctypes.byref(inst))
    print("vkCreateInstance (API 1.1, %s): %d" % (" ".join(enable), r))
    if r != 0:
        return 1
    n = u32()
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(n), None)
    devs = (c_p * n.value)()
    vk.vkEnumeratePhysicalDevices(inst, ctypes.byref(n), devs)
    pd = c_p(devs[0])
    p = VkProps()
    vk.vkGetPhysicalDeviceProperties(pd, ctypes.byref(p))
    print("device: %s, API %s, driver 0x%x, vendor 0x%x device 0x%x type %d" % (
        p.deviceName.decode(), ver(p.apiVersion), p.driverVersion, p.vendorID, p.deviceID, p.deviceType))
    dexts = enum_exts(vk, pd)
    print("device extensions (%d): %s" % (len(dexts), " ".join(dexts)))
    print("device extensions Dolphin looks for: " + ", ".join("%s=%s" % (x, x in dexts) for x in DOLPHIN_DEVICE_EXTS))

    sub = VkSubgroupProps(1000094000, None)
    drv = VkDriverProps(1000196000, None)
    if "VK_KHR_driver_properties" in dexts:
        sub.pNext = ctypes.cast(ctypes.pointer(drv), c_p)
    p2 = VkProps2(1000059001, ctypes.cast(ctypes.pointer(sub), c_p))
    gp2 = vk.vkGetInstanceProcAddr(inst, b"vkGetPhysicalDeviceProperties2")
    if gp2:
        ctypes.CFUNCTYPE(None, c_p, c_p)(gp2)(pd, ctypes.byref(p2))
        ops = sub.supportedOperations
        names = [n for b, n in ((1, "basic"), (2, "vote"), (4, "arithmetic"), (8, "ballot"), (16, "shuffle"),
                                (32, "shuffle_relative"), (64, "clustered"), (128, "quad")) if ops & b]
        required = 1 | 4 | 8 | 16
        print("subgroup: size %d, stages 0x%x (fragment %s), operations %s" % (
            sub.subgroupSize, sub.supportedStages, bool(sub.supportedStages & 0x10), names))
        print("  Dolphin's shaderSubgroupOperations (basic+arithmetic+ballot+shuffle in fragment): %s" % (
            (ops & required) == required and bool(sub.supportedStages & 0x10)))
        if sub.pNext:
            print("driver properties: id %d, name '%s', info '%s'" % (
                drv.driverID, drv.driverName.decode(), drv.driverInfo.decode()))
    f = VkFeatures()
    vk.vkGetPhysicalDeviceFeatures(pd, ctypes.byref(f))
    print("features Dolphin reads:")
    for name in DOLPHIN_FEATURES:
        print("  %-42s %s" % (name, bool(getattr(f, name))))
    print("other features: " + " ".join(n for n in FEATURES if getattr(f, n) and n not in DOLPHIN_FEATURES))
    L = p.limits
    for name in ("maxImageDimension2D", "maxTexelBufferElements", "maxUniformBufferRange", "maxStorageBufferRange",
                 "maxPushConstantsSize", "minUniformBufferOffsetAlignment", "minStorageBufferOffsetAlignment",
                 "minTexelBufferOffsetAlignment", "bufferImageGranularity", "nonCoherentAtomSize",
                 "optimalBufferCopyOffsetAlignment", "optimalBufferCopyRowPitchAlignment",
                 "framebufferColorSampleCounts", "framebufferDepthSampleCounts", "maxSamplerAnisotropy",
                 "maxFragmentDualSrcAttachments", "maxGeometryOutputVertices", "maxComputeSharedMemorySize",
                 "maxClipDistances", "maxColorAttachments", "timestampPeriod"):
        v = getattr(L, name)
        print("  %-42s %s" % (name, v if not isinstance(v, int) or "Sample" not in name else hex(v)))
    print("  %-42s %s" % ("pointSizeRange", list(L.pointSizeRange)))
    print("format support (optimal tiling features; 0x200 = depth/stencil attachment, 0x80 = color attachment, 0x1 = sampled):")
    for fmt, name in DEPTH_FORMATS:
        fp = VkFormatProps()
        vk.vkGetPhysicalDeviceFormatProperties(pd, fmt, ctypes.byref(fp))
        print("  %-28s optimal 0x%05x buffer 0x%04x" % (name, fp.optimal, fp.buffer))

    rc = 0
    if window:
        rc = vulkan_window(vk, inst, pd)
    vk.vkDestroyInstance(inst, None)
    return rc


def vulkan_window(vk, inst, pd):
    def ip(name, restype, *args):
        a = vk.vkGetInstanceProcAddr(inst, name.encode())
        return ctypes.CFUNCTYPE(restype, *args)(a) if a else None
    create_surface = ip("vkCreateWaylandSurfaceKHR", ctypes.c_int, c_p, c_p, c_p, c_p)
    if not create_surface:
        print("vkCreateWaylandSurfaceKHR not available")
        return 1
    w = Wayland()
    top = Toplevel(w, title=b"dolphin-ut stage 0 vulkan probe")
    top.report()
    sci = VkWaylandSurfaceCI(1000006000, None, 0, c_p(w.display), c_p(top.surface))
    surface = _u64()
    r = create_surface(inst, ctypes.byref(sci), None, ctypes.byref(surface))
    print("vkCreateWaylandSurfaceKHR: %d" % r)
    if r != 0:
        return 1
    n = u32()
    vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, ctypes.byref(n), None)
    fams = (VkQueueFamily * n.value)()
    vk.vkGetPhysicalDeviceQueueFamilyProperties(pd, ctypes.byref(n), fams)
    support = ip("vkGetPhysicalDeviceSurfaceSupportKHR", ctypes.c_int, c_p, u32, _u64, c_p)
    for i, fam in enumerate(fams):
        s = u32()
        support(pd, i, surface, ctypes.byref(s))
        print("queue family %d: flags 0x%x count %d, present support %d" % (i, fam.queueFlags, fam.queueCount, s.value))
    caps = VkSurfaceCaps()
    r = ip("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", ctypes.c_int, c_p, _u64, c_p)(pd, surface, ctypes.byref(caps))
    print("surface capabilities (%d): images %d..%d, currentExtent %dx%d, min %dx%d, max %dx%d, transforms 0x%x "
          "current 0x%x, compositeAlpha 0x%x, usage 0x%x" % (
              r, caps.minImageCount, caps.maxImageCount, caps.currentExtent.width, caps.currentExtent.height,
              caps.minImageExtent.width, caps.minImageExtent.height, caps.maxImageExtent.width,
              caps.maxImageExtent.height, caps.supportedTransforms, caps.currentTransform,
              caps.supportedCompositeAlpha, caps.supportedUsageFlags))
    gf = ip("vkGetPhysicalDeviceSurfaceFormatsKHR", ctypes.c_int, c_p, _u64, c_p, c_p)
    gf(pd, surface, ctypes.byref(n), None)
    fmts = (VkSurfaceFormat * n.value)()
    gf(pd, surface, ctypes.byref(n), fmts)
    print("surface formats: " + ", ".join("%s/%d" % (FORMATS.get(x.format, x.format), x.colorSpace) for x in fmts))
    gm = ip("vkGetPhysicalDeviceSurfacePresentModesKHR", ctypes.c_int, c_p, _u64, c_p, c_p)
    gm(pd, surface, ctypes.byref(n), None)
    modes = (ctypes.c_int * n.value)()
    gm(pd, surface, ctypes.byref(n), modes)
    print("present modes: " + ", ".join(PRESENT_MODES.get(m, str(m)) for m in modes))

    # A device with the swapchain extension, then one swapchain and one acquire.
    prio = (ctypes.c_float * 1)(1.0)
    q = VkQueueCI(2, None, 0, 0, 1, prio)
    exts = strarr(["VK_KHR_swapchain"])
    dci = VkDeviceCI(3, None, 0, 1, ctypes.pointer(q), 0, None, 1, ctypes.cast(exts, c_p), None)
    dev = c_p()
    r = vk.vkCreateDevice(pd, ctypes.byref(dci), None, ctypes.byref(dev))
    print("vkCreateDevice with VK_KHR_swapchain: %d" % r)
    if r != 0:
        return 1
    vk.vkGetDeviceProcAddr.restype = c_p
    vk.vkGetDeviceProcAddr.argtypes = [c_p, ctypes.c_char_p]

    def dp(name, restype, *args):
        return ctypes.CFUNCTYPE(restype, *args)(vk.vkGetDeviceProcAddr(dev, name.encode()))
    width, height = top.size()
    if caps.currentExtent.width != 0xFFFFFFFF:
        width, height = caps.currentExtent.width, caps.currentExtent.height
    fmt = fmts[0]
    sc_ci = VkSwapchainCI(1000001000, None, 0, surface.value, max(caps.minImageCount, 3), fmt.format, fmt.colorSpace,
                          VkExtent2D(width, height), 1, 0x10, 0, 0, None, caps.currentTransform or 1,
                          caps.supportedCompositeAlpha & -caps.supportedCompositeAlpha, 2, 1, 0)
    sc = _u64()
    r = dp("vkCreateSwapchainKHR", ctypes.c_int, c_p, c_p, c_p, c_p)(dev, ctypes.byref(sc_ci), None, ctypes.byref(sc))
    print("vkCreateSwapchainKHR (FIFO, %dx%d, %s): %d" % (width, height, FORMATS.get(fmt.format, fmt.format), r))
    if r == 0:
        cnt = u32()
        dp("vkGetSwapchainImagesKHR", ctypes.c_int, c_p, _u64, c_p, c_p)(dev, sc, ctypes.byref(cnt), None)
        print("swapchain images: %d" % cnt.value)

        class VkFenceCI(ctypes.Structure):
            _fields_ = [("sType", ctypes.c_int), ("pNext", c_p), ("flags", u32)]
        fence = _u64()
        dp("vkCreateFence", ctypes.c_int, c_p, c_p, c_p, c_p)(dev, ctypes.byref(VkFenceCI(8, None, 0)), None,
                                                              ctypes.byref(fence))
        idx = u32()
        r = dp("vkAcquireNextImageKHR", ctypes.c_int, c_p, _u64, _u64, _u64, _u64, c_p)(
            dev, sc, 1000000000, 0, fence, ctypes.byref(idx))
        print("vkAcquireNextImageKHR: %d (image %d)" % (r, idx.value))
        if r == 0:
            dp("vkWaitForFences", ctypes.c_int, c_p, u32, c_p, u32, _u64)(
                dev, 1, ctypes.byref(fence), 1, 1000000000)
        dp("vkDestroyFence", None, c_p, _u64, c_p)(dev, fence, None)
        dp("vkDestroySwapchainKHR", None, c_p, _u64, c_p)(dev, sc, None)
    # What Dolphin (VKSwapChain::CreateSwapChain) asks for: the window's size, COLOR_ATTACHMENT usage,
    # VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR whatever the surface reports, identity transform.
    ww, wh = top.size()
    for mode in (2, 1):
        for alpha in (0x1, 0x8):
            sc_ci = VkSwapchainCI(1000001000, None, 0, surface.value, max(caps.minImageCount, 3), fmt.format,
                                  fmt.colorSpace, VkExtent2D(ww, wh), 1, 0x10, 0, 0, None, 1, alpha, mode, 1, 0)
            sc = _u64()
            r = dp("vkCreateSwapchainKHR", ctypes.c_int, c_p, c_p, c_p, c_p)(dev, ctypes.byref(sc_ci), None,
                                                                            ctypes.byref(sc))
            print("Dolphin-style swapchain %dx%d %s compositeAlpha 0x%x: %d" % (
                ww, wh, PRESENT_MODES[mode], alpha, r))
            if r == 0:
                dp("vkDestroySwapchainKHR", None, c_p, _u64, c_p)(dev, sc, None)
    print("libhybris Vulkan platforms loaded: %s" % loaded("vulkanplatform_"))
    vk.vkDestroyDevice(dev, None)
    ip("vkDestroySurfaceKHR", None, c_p, _u64, c_p)(inst, surface, None)
    return 0


if __name__ == "__main__":
    args = sys.argv[1:]
    mode = args[0] if args else "globals"
    frames = int(args[args.index("--frames") + 1]) if "--frames" in args else 240
    faulthandler.enable()
    if mode == "globals":
        sys.exit(mode_globals())
    if mode in ("egl", "egl-window"):
        sys.exit(mode_egl(window=mode == "egl-window", fullscreen="--fullscreen" in args, frames=frames))
    if mode in ("vulkan", "vulkan-window"):
        sys.exit(mode_vulkan(window=mode == "vulkan-window"))
    sys.exit(__doc__)
