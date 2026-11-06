/* Copyright (C) 2010-2012 Canonical Ltd.
 * C port of unity-shared/IconRenderer.cpp and LauncherIcon::DrawCountTexture.
 * Original projection, texture layers, color arithmetic and indicators retained.
 * Both backends use the same geometry; the GL backend is a test reference.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "renderer.h"
#ifdef LAUNCHER_RENDER_GL
#include <epoxy/gl.h>
#endif
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    BACK,
    EDGE,
    SHINE,
    GLOW,
    SHADOW,
    SELECTED,
    ARROW,
    EMPTY,
    ACTIVE,
    PIP,
    TROUGH,
    FILL,
    ATTENTION,
    WHITE,
    TEXTURES
};
typedef struct {
    float x, y, w, u, v, r, g, b, a;
} Vertex;
typedef struct {
    int x, y, w, h;
#ifndef LAUNCHER_RENDER_GL
    cairo_surface_t *surface;
#endif
} Sprite;
typedef struct {
    int x, y;
} Slot;
typedef struct {
    GdkPixbuf *textures[TEXTURES], *shortcuts[10];
    int tile, image, marker, glow_size, scale, cell, atlas_size, maximum;
    int width, height, content_width, content_origin;
    guint beacon_start, shelf_start, icons_start;
    double shelf_top;
    double hide_progress;
    RenderStyle style;
#ifdef LAUNCHER_RENDER_GL
    GLuint program, atlas, vao, vbo, copy_fbo;
    GLint viewport_uniform;
#else
    cairo_surface_t *frame;
    GPtrArray *sources;
    gsize image_bytes;
#endif
    GHashTable *sprites;
    GArray *slots, *vertices;
    gsize buffer_capacity;
    gboolean trim_pending;
    guchar *upload;
    RenderStats stats;
} Renderer;

typedef struct {
    const guchar *data;
    gsize length, offset;
} PngReader;

static cairo_status_t read_png(void *closure, unsigned char *out, unsigned length)
{
    PngReader *reader = closure;
    if (length > reader->length - reader->offset)
        return CAIRO_STATUS_READ_ERROR;
    memcpy(out, reader->data + reader->offset, length);
    reader->offset += length;
    return CAIRO_STATUS_SUCCESS;
}

GdkPixbuf *launcher_resource_icon(const char *name, int size, GError **error)
{
    int base = strstr(name, "launcher_mark") ? 48 : strstr(name, "fill") ? 42 : 54;
    const char *suffix = strrchr(name, '_');
    if (suffix && g_ascii_isdigit(suffix[1]))
        base = atoi(suffix + 1);
    int factor = g_str_has_suffix(name, ".png") ? 1
                 : size <= base                 ? 1
                 : size <= base * 2             ? 2
                 : size <= base * 3             ? 3
                 : size <= base * 4             ? 4
                                                : 8;
    char *path = g_strdup_printf("/org/xoxorwr/lunatic/launcher/%s/%d", name, factor);
    GBytes *bytes = g_resources_lookup_data(path, G_RESOURCE_LOOKUP_FLAGS_NONE, error);
    g_free(path);
    if (!bytes)
        return NULL;
    PngReader reader = {0};
    reader.data = g_bytes_get_data(bytes, &reader.length);
    cairo_surface_t *surface = cairo_image_surface_create_from_png_stream(read_png, &reader);
    GdkPixbuf *result = NULL;
    cairo_status_t status = cairo_surface_status(surface);
    if (status == CAIRO_STATUS_SUCCESS) {
        int w = cairo_image_surface_get_width(surface), h = cairo_image_surface_get_height(surface);
        GdkPixbuf *native = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
        double ratio = (double)size / MAX(w, h);
        result = w == size || h == size
                     ? g_object_ref(native)
                     : gdk_pixbuf_scale_simple(native, MAX(1, (int)round(w * ratio)),
                                               MAX(1, (int)round(h * ratio)), GDK_INTERP_BILINEAR);
        g_object_unref(native);
    } else
        g_set_error(error, G_RESOURCE_ERROR, G_RESOURCE_ERROR_INTERNAL,
                    "Cannot decode bundled artwork %s: %s", name, cairo_status_to_string(status));
    cairo_surface_destroy(surface);
    g_bytes_unref(bytes);
    return result;
}

#ifdef LAUNCHER_RENDER_GL
static GQuark render_error(void)
{
    return g_quark_from_static_string("lunatic-renderer-error");
}
#endif

static void free_sprite(gpointer value)
{
#ifndef LAUNCHER_RENDER_GL
    Sprite *sprite = value;
    cairo_surface_destroy(sprite->surface);
#endif
    g_free(value);
}

static GdkPixbuf *make_label(Renderer *r, const char *text, int kind)
{
    gboolean shortcut = kind == 1, window_count = kind == 2;
    /* LauncherIcon::DrawCountTexture, rendered only when the count changes.
     * Cairo/Pango rasterizes at the actual monitor scale, not logical pixels.
     */
    cairo_surface_t *measure = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(measure);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string("Ubuntu");
    double scaling = r->tile / 54.0;
    pango_font_description_set_absolute_size(
        font, (shortcut ? r->tile * .33 : (window_count ? 11 : 16) * scaling) * PANGO_SCALE);
    if (!shortcut)
        pango_font_description_set_weight(font, PANGO_WEIGHT_BOLD);
    pango_layout_set_font_description(layout, font);
    pango_layout_set_width(layout, (int)(r->image * .75 * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_MIDDLE);
    pango_layout_set_text(layout, text, -1);
    PangoRectangle ink;
    pango_layout_get_pixel_extents(layout, &ink, NULL);
    double height = MAX((shortcut || window_count ? 20 : 28) * scaling, ink.height + 8 * scaling),
           inset = height / 2;
    double width = !shortcut && strlen(text) == 1 ? height
                                                  : MAX(height, ink.width + (window_count ? 6
                                                                             : shortcut   ? 12
                                                                                          : 14) *
                                                                                scaling);
    int w = MAX(1, (int)ceil(width * r->scale)), h = MAX(1, (int)ceil(height * r->scale));
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *paint = cairo_create(surface);
    cairo_scale(paint, r->scale, r->scale);
    cairo_move_to(paint, inset, height - 1);
    cairo_arc(paint, inset, inset, MAX(1, inset - 1), .5 * G_PI, 1.5 * G_PI);
    cairo_arc(paint, width - inset, inset, MAX(1, inset - 1), 1.5 * G_PI, .5 * G_PI);
    cairo_close_path(paint);
    if (shortcut)
        cairo_set_source_rgba(paint, .35, .35, .35, .9);
    else if (window_count)
        cairo_set_source_rgb(paint, .16, .16, .18);
    else
        cairo_set_source_rgb(paint, .9, .08, .12);
    cairo_fill_preserve(paint);
    cairo_set_source_rgb(paint, 1, 1, 1);
    cairo_set_line_width(paint, shortcut ? 2 : 1 * scaling);
    cairo_stroke(paint);
    cairo_move_to(paint, (width - ink.width) / 2 - ink.x, (height - ink.height) / 2 - ink.y);
    pango_cairo_show_layout(paint, layout);
    GdkPixbuf *p = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
    cairo_destroy(paint);
    cairo_surface_destroy(surface);
    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_destroy(cr);
    cairo_surface_destroy(measure);
    return p;
}

gboolean icon_renderer_init(IconRenderer *renderer, int tile, int image, int scale, GError **error)
{
    Renderer *r = g_new0(Renderer, 1);
    renderer->state = r;
    r->style = (RenderStyle){.color = {95 / 255.0, 18 / 255.0, 45 / 255.0},
                             .opacity = .6667,
                             .hide_animation = HIDE_FADE_SLIDE};
    r->tile = tile;
    r->image = image;
    r->scale = CLAMP(scale, 1, 4);
    r->glow_size = (int)round(tile * 62.0 / 54);
    r->marker = MAX(1, (int)round(tile * 19.0 / 54));
    r->cell = MAX(r->glow_size, tile) * r->scale + 4;
#ifndef LAUNCHER_RENDER_GL
    r->sources = g_ptr_array_new_with_free_func((GDestroyNotify)cairo_surface_destroy);
#endif
    r->sprites = g_hash_table_new_full(g_direct_hash, g_direct_equal, g_object_unref, free_sprite);
    r->vertices = g_array_sized_new(FALSE, FALSE, sizeof(Vertex), 2048);
#ifdef LAUNCHER_RENDER_GL
    r->slots = g_array_sized_new(FALSE, FALSE, sizeof(Slot), 128);
    r->upload = g_malloc0((gsize)r->cell * r->cell * 4);
#endif
    struct {
        const char *name;
        int size;
    } textures[TEXTURES - 1] = {{"launcher_icon_back_54.svg", tile},
                                {"launcher_icon_edge_54.svg", tile},
                                {"launcher_icon_shine_54.svg", tile},
                                {"launcher_icon_glow_62.png", r->glow_size},
                                {"launcher_icon_shadow_62.svg", r->glow_size},
                                {"launcher_icon_selected_back_54.svg", tile},
                                {"launcher_arrow_ltr_19.svg", r->marker},
                                {"launcher_arrow_outline_ltr_19.svg", r->marker},
                                {"launcher_arrow_rtl_19.svg", r->marker},
                                {"launcher_pip_ltr_19.svg", r->marker},
                                {"progress_bar_trough.svg", tile},
                                {"progress_bar_fill.svg", image - (tile - image)},
                                {"attention-halo.png", r->glow_size}};
    for (guint i = 0; i < G_N_ELEMENTS(textures); i++) {
        r->textures[i] =
            launcher_resource_icon(textures[i].name, textures[i].size * r->scale, error);
        if (!r->textures[i]) {
            icon_renderer_clear(renderer);
            return FALSE;
        }
    }
    r->textures[WHITE] = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(r->textures[WHITE], 0xffffffff);
    return TRUE;
}

#ifdef LAUNCHER_RENDER_GL
static GLuint compile_shader(GLenum kind, const char *source, GError **error)
{
    GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        g_set_error(error, render_error(), 1, "Cannot compile launcher shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

gboolean icon_renderer_realize(IconRenderer *renderer, GError **error)
{
    Renderer *r = renderer->state;
    const char *vertex =
        "#version 150\n"
        "in vec3 position; in vec2 texcoord; in vec4 color;\n"
        "uniform vec4 viewport; out vec2 uv; out vec4 tint;\n"
        "void main() {\n"
        " gl_Position=vec4((2.0*(position.x+viewport.z)/viewport.x-1.0)*position.z,\n"
        " (1.0-2.0*position.y/viewport.y)*position.z,0.0,position.z);\n"
        " uv=texcoord; tint=vec4(color.rgb*color.a,color.a)*viewport.w; }\n";
    const char *fragment =
        "#version 150\n"
        "uniform sampler2D atlas; in vec2 uv; in vec4 tint; out vec4 pixel;\n"
        "void main(){ pixel=texture(atlas,uv/vec2(textureSize(atlas,0)))*tint; }\n";
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex, error);
    if (!vs)
        return FALSE;
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment, error);
    if (!fs) {
        glDeleteShader(vs);
        return FALSE;
    }
    r->program = glCreateProgram();
    glAttachShader(r->program, vs);
    glAttachShader(r->program, fs);
    glBindAttribLocation(r->program, 0, "position");
    glBindAttribLocation(r->program, 1, "texcoord");
    glBindAttribLocation(r->program, 2, "color");
    glLinkProgram(r->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok;
    glGetProgramiv(r->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(r->program, sizeof(log), NULL, log);
        g_set_error(error, render_error(), 2, "Cannot link launcher shader: %s", log);
        icon_renderer_unrealize(renderer);
        return FALSE;
    }
    r->viewport_uniform = glGetUniformLocation(r->program, "viewport");
    glUseProgram(r->program);
    glUniform1i(glGetUniformLocation(r->program, "atlas"), 0);
    glGenVertexArrays(1, &r->vao);
    glGenBuffers(1, &r->vbo);
    glGenFramebuffers(1, &r->copy_fbo);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &r->maximum);
    r->maximum = MIN(r->maximum, 4096);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, FALSE, sizeof(Vertex), (void *)offsetof(Vertex, x));
    glVertexAttribPointer(1, 2, GL_FLOAT, FALSE, sizeof(Vertex), (void *)offsetof(Vertex, u));
    glVertexAttribPointer(2, 4, GL_FLOAT, FALSE, sizeof(Vertex), (void *)offsetof(Vertex, r));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    return TRUE;
}

void icon_renderer_unrealize(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    if (!r)
        return;
    if (r->program)
        glDeleteProgram(r->program);
    if (r->atlas)
        glDeleteTextures(1, &r->atlas);
    if (r->vbo)
        glDeleteBuffers(1, &r->vbo);
    if (r->vao)
        glDeleteVertexArrays(1, &r->vao);
    if (r->copy_fbo)
        glDeleteFramebuffers(1, &r->copy_fbo);
    r->program = r->atlas = r->vbo = r->vao = r->copy_fbo = 0;
    r->atlas_size = 0;
    r->buffer_capacity = 0;
    g_array_set_size(r->slots, 0);
    g_hash_table_remove_all(r->sprites);
}

#else
gboolean icon_renderer_realize(IconRenderer *renderer, GError **error)
{
    (void)renderer;
    (void)error;
    return TRUE;
}

void icon_renderer_unrealize(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    if (!r)
        return;
    g_ptr_array_set_size(r->sources, 0);
    g_hash_table_remove_all(r->sprites);
    r->image_bytes = 0;
    if (r->frame)
        cairo_surface_destroy(r->frame);
    r->frame = NULL;
}
#endif

void icon_renderer_clear(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    if (!r)
        return;
    /* GPU objects must first be released by the GLArea unrealize handler. */
#ifdef LAUNCHER_RENDER_GL
    g_assert(r->program == 0);
#else
    icon_renderer_unrealize(renderer);
    g_ptr_array_unref(r->sources);
#endif
    for (guint i = 0; i < TEXTURES; i++)
        g_clear_object(&r->textures[i]);
    for (guint i = 0; i < 10; i++)
        g_clear_object(&r->shortcuts[i]);
    g_hash_table_unref(r->sprites);
    g_array_unref(r->vertices);
#ifdef LAUNCHER_RENDER_GL
    g_array_unref(r->slots);
    g_free(r->upload);
#endif
    g_free(r);
    renderer->state = NULL;
}

void icon_renderer_forget(IconRenderer *renderer, GdkPixbuf *image)
{
    Renderer *r = renderer->state;
    if (!r || !image)
        return;
    Sprite *sprite = g_hash_table_lookup(r->sprites, image);
    if (sprite) {
#ifdef LAUNCHER_RENDER_GL
        Slot slot = {sprite->x - 2, sprite->y - 2};
        g_array_append_val(r->slots, slot);
#else
        r->image_bytes -= (gsize)cairo_image_surface_get_stride(sprite->surface) *
                          cairo_image_surface_get_height(sprite->surface);
#endif
        g_hash_table_remove(r->sprites, image);
        r->trim_pending = TRUE;
    }
}

static void clear_badge(IconRenderer *renderer, RenderCache *cache)
{
    icon_renderer_forget(renderer, cache->badge);
    g_clear_object(&cache->badge);
}

void icon_renderer_cache_clear(IconRenderer *renderer, RenderCache *cache)
{
    clear_badge(renderer, cache);
    icon_renderer_forget(renderer, cache->window_count);
    g_clear_object(&cache->window_count);
}

#ifdef LAUNCHER_RENDER_GL
static gboolean grow_atlas(Renderer *r)
{
    int old = r->atlas_size, size = old ? MIN(old * 2, r->maximum) : 512;
    if (size <= old || size < r->cell)
        return FALSE;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (old) {
        GLint framebuffer;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, r->copy_fbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->atlas,
                               0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, old, old);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)framebuffer);
        glDeleteTextures(1, &r->atlas);
    }
    int old_cells = old / r->cell, cells = size / r->cell;
    for (int y = cells - 1; y >= 0; y--)
        for (int x = cells - 1; x >= 0; x--)
            if (x >= old_cells || y >= old_cells) {
                Slot slot = {x * r->cell, y * r->cell};
                g_array_append_val(r->slots, slot);
            }
    r->atlas = texture;
    r->atlas_size = size;
    r->stats.atlas_bytes = (gsize)size * size * 4;
    return TRUE;
}

/* Reclaim a temporary high-water mark. Repack on the GPU only at frame start,
 * while there are no queued vertices referring to the old atlas coordinates. */
static void trim_atlas(Renderer *r)
{
    if (!r->trim_pending || !r->atlas)
        return;
    r->trim_pending = FALSE;
    guint count = g_hash_table_size(r->sprites);
    int size = 512;
    while ((guint)((size / r->cell) * (size / r->cell)) < count && size < r->maximum)
        size *= 2;
    if (size >= r->atlas_size)
        return;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GLint framebuffer;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, r->copy_fbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r->atlas, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    int columns = size / r->cell;
    guint index = 0;
    GHashTableIter iter;
    gpointer value;
    g_hash_table_iter_init(&iter, r->sprites);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        Sprite *sprite = value;
        int x = (int)(index % (guint)columns) * r->cell,
            y = (int)(index / (guint)columns) * r->cell;
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x, y, sprite->x - 2, sprite->y - 2, r->cell, r->cell);
        sprite->x = x + 2;
        sprite->y = y + 2;
        index++;
    }
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)framebuffer);
    glDeleteTextures(1, &r->atlas);
    r->atlas = texture;
    r->atlas_size = size;
    g_array_set_size(r->slots, 0);
    for (guint i = (guint)(columns * columns); i > index; i--) {
        Slot slot = {(int)((i - 1) % (guint)columns) * r->cell,
                     (int)((i - 1) / (guint)columns) * r->cell};
        g_array_append_val(r->slots, slot);
    }
    r->stats.atlas_bytes = (gsize)size * size * 4;
    if (r->buffer_capacity > 2048 * sizeof(Vertex)) {
        glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
        r->buffer_capacity = 2048 * sizeof(Vertex);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)r->buffer_capacity, NULL, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        g_array_unref(r->vertices);
        r->vertices = g_array_sized_new(FALSE, FALSE, sizeof(Vertex), 2048);
    }
}

static Sprite *ensure_image(Renderer *r, GdkPixbuf *image)
{
    if (!image)
        return NULL;
    Sprite *sprite = g_hash_table_lookup(r->sprites, image);
    if (sprite)
        return sprite;
    if (!r->slots->len && !grow_atlas(r))
        return NULL;
    int w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
    GdkPixbuf *scaled = NULL;
    if (w + 4 > r->cell || h + 4 > r->cell) {
        double ratio = (double)(r->image * r->scale) / MAX(w, h);
        w = MAX(1, (int)round(w * ratio));
        h = MAX(1, (int)round(h * ratio));
        scaled = gdk_pixbuf_scale_simple(image, w, h, GDK_INTERP_BILINEAR);
    }
    GdkPixbuf *pixels = scaled ? scaled : image;
    Slot slot = g_array_index(r->slots, Slot, r->slots->len - 1);
    g_array_set_size(r->slots, r->slots->len - 1);
    memset(r->upload, 0, (gsize)r->cell * r->cell * 4);
    int stride = gdk_pixbuf_get_rowstride(pixels), channels = gdk_pixbuf_get_n_channels(pixels);
    const guchar *data = gdk_pixbuf_read_pixels(pixels);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const guchar *in = data + y * stride + x * channels;
            guchar *out = r->upload + ((y + 2) * r->cell + x + 2) * 4;
            guint alpha = channels == 4 ? in[3] : 255;
            for (int k = 0; k < 3; k++)
                out[k] = (guchar)((in[k] * alpha + 127) / 255);
            out[3] = (guchar)alpha;
        }
    glBindTexture(GL_TEXTURE_2D, r->atlas);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, slot.x, slot.y, r->cell, r->cell, GL_RGBA, GL_UNSIGNED_BYTE,
                    r->upload);
    g_clear_object(&scaled);
    sprite = g_new(Sprite, 1);
    *sprite = (Sprite){slot.x + 2, slot.y + 2, w, h};
    g_hash_table_insert(r->sprites, g_object_ref(image), sprite);
    r->stats.image_updates++;
    return sprite;
}

#else
#include "renderer-software.inc"
#endif

static void quad(Renderer *r, GdkPixbuf *image, double left, double top, double w, double h,
                 const double color[3], double alpha, double cx, double cy, double z, double rx,
                 double rz, double extent, double crop)
{
    if (!image || alpha <= 0 || w <= 0 || h <= 0 || crop <= 0)
        return;
    Sprite *sprite = ensure_image(r, image);
    if (!sprite)
        return;
    Vertex vertices[6];
    const int corners[6][2] = {{0, 0}, {0, 1}, {1, 1}, {0, 0}, {1, 1}, {1, 0}};
    double cosine = cos(rz), sine = sin(rz), ax = cos(rx), sx = sin(rx);
    for (guint i = 0; i < 6; i++) {
        int x = corners[i][0], y = corners[i][1];
        double u = left + x * w * crop - cx, v = top + y * h - cy;
        double rotated_y = sine * u + cosine * v;
        double denominator = 1 + 6 * (z + sx * rotated_y) / extent;
        vertices[i] =
            (Vertex){(float)(r->content_origin + cx + (cosine * u - sine * v) / denominator),
                     (float)(cy + ax * rotated_y / denominator),
                     (float)denominator,
                     (float)(sprite->x + (sprite->w == 1 ? .5 : x * sprite->w * crop)),
                     (float)(sprite->y + (sprite->h == 1 ? .5 : y * sprite->h)),
                     (float)color[0],
                     (float)color[1],
                     (float)color[2],
                     (float)MAX(alpha, 0)};
    }
    g_array_append_vals(r->vertices, vertices, 6);
#ifndef LAUNCHER_RENDER_GL
    g_ptr_array_add(r->sources, cairo_surface_reference(sprite->surface));
#endif
}

void icon_renderer_begin(IconRenderer *renderer, int width, int height, double hide_progress)
{
    Renderer *r = renderer->state;
    r->width = width;
    r->content_width = r->style.dock_width > 0 ? MIN(width, r->style.dock_width) : width;
    r->content_origin = r->style.right ? width - r->content_width : 0;
    r->height = MAX(1, height);
    r->hide_progress = hide_progress;
    r->beacon_start = r->shelf_start = G_MAXUINT;
    r->shelf_top = height;
    r->stats.draw_calls = r->stats.image_updates = r->stats.vertices = 0;
    g_array_set_size(r->vertices, 0);
#ifdef LAUNCHER_RENDER_GL
    glActiveTexture(GL_TEXTURE0);
    trim_atlas(r);
    glViewport(0, 0, width * r->scale, height * r->scale);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
#else
    g_ptr_array_set_size(r->sources, 0);
    if (!prepare_frame(r))
        return;
#endif
    quad(r, r->textures[WHITE], 0, 0, r->content_width, height, r->style.color, r->style.opacity,
         r->content_width / 2.0, height / 2.0, 0, 0, 0, height, 1);
    double edge[3] = {.7, .7, .7};
    double border = r->style.right ? 0 : r->content_width - 1;
    quad(r, r->textures[WHITE], border, 0, 1, height, edge, .15 * r->style.opacity / .6667,
         border + .5, height / 2.0, 0, 0, 0, height, 1);
    r->icons_start = r->vertices->len;
}

void icon_renderer_shelf(IconRenderer *renderer, double top)
{
    Renderer *r = renderer->state;
    r->shelf_start = r->vertices->len;
    r->shelf_top = CLAMP(top, 0, r->height);
}

void icon_renderer_draw(IconRenderer *renderer, const RenderArg *arg, RenderCache *cache,
                        const LayoutIcon *layout, double cx, double extent, double width)
{
    Renderer *r = renderer->state;
    const double white[3] = {1, 1, 1};
    double maximum = MAX(r->style.color[0], MAX(r->style.color[1], r->style.color[2]));
    double bg[3];
    for (int i = 0; i < 3; i++)
        bg[i] = maximum > 0 ? r->style.color[i] / maximum : 0;
    double color[3], edge_color[3], shadow[3];
    double light = arg->selected ? .95 : arg->backlight;
    gboolean colorify = arg->system || light == 0;
    for (int k = 0; k < 3; k++) {
        double tint = arg->selected ? 1 : colorify ? bg[k] * .7 : 1;
        color[k] = (arg->selected || colorify ? 1 : arg->background[k]) * tint;
        edge_color[k] = 1 + ((colorify || arg->selected ? 1 : arg->background[k]) - 1) * light;
        shadow[k] = tint * .3;
    }
    double marker_center = cx;
    cx += (r->style.right ? -1 : 1) * arg->bounce;
    double cy = layout->center, tile = r->tile, glow = r->glow_size;
#define LAYER(pic, size, rgb, opacity)                                                             \
    quad(r, (pic), cx - (size) / 2.0, cy - (size) / 2.0, (size), (size), (rgb),                    \
         (opacity) * arg->alpha, cx, cy, layout->z, layout->rotation, arg->rotation_z, extent, 1)
    if (!arg->selected)
        LAYER(r->textures[SHADOW], glow, shadow, .6);
    LAYER(r->textures[arg->selected ? SELECTED : BACK], tile, color, light);
    LAYER(r->textures[EDGE], tile, edge_color, arg->alpha);
    LAYER(arg->image, r->image, white, 1);
    LAYER(r->textures[SHINE], tile, white, 1);
    /* The outer ring must survive opaque application icons. Its hollow center
     * preserves icon detail while the rim and falloff sit above the artwork. */
    if (!arg->selected && arg->glow_intensity > 0)
        LAYER(r->textures[ATTENTION], glow, arg->glow, arg->glow_intensity);
    if (arg->selected)
        LAYER(r->textures[GLOW], glow, white, 1);
#undef LAYER
    if (arg->progress_visible) {
        GdkPixbuf *trough = r->textures[TROUGH], *fill = r->textures[FILL];
        double tw = gdk_pixbuf_get_width(trough) / (double)r->scale;
        double th = gdk_pixbuf_get_height(trough) / (double)r->scale;
        double fw = gdk_pixbuf_get_width(fill) / (double)r->scale;
        double fh = gdk_pixbuf_get_height(fill) / (double)r->scale;
        quad(r, trough, cx - tw / 2, cy - th / 2, tw, th, white, arg->alpha, cx, cy, layout->z,
             layout->rotation, arg->rotation_z, extent, 1);
        quad(r, fill, cx - tw / 2 + r->image * .125, cy - fh / 2, fw, fh, white, arg->alpha, cx, cy,
             layout->z, layout->rotation, arg->rotation_z, extent, CLAMP(arg->progress, 0, 1));
    }
    if (arg->count_visible && arg->count > 0) {
        if (!cache->badge || cache->count != arg->count) {
            clear_badge(renderer, cache);
            char text[32];
            if (arg->count > 99)
                g_strlcpy(text, "99+", sizeof(text));
            else
                g_snprintf(text, sizeof(text), "%" G_GINT64_FORMAT, arg->count);
            cache->badge = make_label(r, text, FALSE);
            cache->count = arg->count;
        }
        if (cache->badge) {
            double w = gdk_pixbuf_get_width(cache->badge) / (double)r->scale;
            double h = gdk_pixbuf_get_height(cache->badge) / (double)r->scale;
            quad(r, cache->badge, cx + tile / 2 - w, cy - tile / 2, w, h, white, arg->alpha, cx, cy,
                 layout->z, layout->rotation, arg->rotation_z, extent, 1);
        }
    } else if (cache->badge)
        clear_badge(renderer, cache);
    if (arg->shortcut >= '0' && arg->shortcut <= '9') {
        guint index = (guint)(arg->shortcut - '0');
        if (!r->shortcuts[index]) {
            char text[] = {arg->shortcut, 0};
            r->shortcuts[index] = make_label(r, text, TRUE);
        }
        double w = gdk_pixbuf_get_width(r->shortcuts[index]) / (double)r->scale;
        double h = gdk_pixbuf_get_height(r->shortcuts[index]) / (double)r->scale;
        quad(r, r->shortcuts[index], cx - w / 2, cy - h / 2, w, h, white, arg->alpha, cx, cy,
             layout->z, layout->rotation, arg->rotation_z, extent, 1);
    }
    double center = cy - (int)(layout->rotation / (2 * G_PI) * tile);
    double marker_color[3] = {.83, .83, .83};
    if (arg->urgent) {
        marker_color[0] = 1;
        marker_color[1] = .65;
        marker_color[2] = .15;
    }
    cx = marker_center; /* Window-count and active markers remain anchored to the dock. */
    double bar_w = MAX(6, round(tile * .15));
    double bar_h = MAX(3, round(tile * .065));
    double gap = MAX(3, round(tile * .065));
    guint limit = MAX(3, MIN(6, (guint)((tile + gap) / (bar_h + gap))));
    if (arg->running_here && arg->running > limit) {
        guint shown = MIN(arg->running, 100);
        if (!cache->window_count || cache->windows != shown) {
            icon_renderer_forget(renderer, cache->window_count);
            g_clear_object(&cache->window_count);
            char text[16];
            if (shown == 100)
                g_strlcpy(text, "99+", sizeof(text));
            else
                g_snprintf(text, sizeof(text), "%u", shown);
            cache->window_count = make_label(r, text, 2);
            cache->windows = shown;
        }
        if (cache->window_count) {
            double w = gdk_pixbuf_get_width(cache->window_count) / (double)r->scale;
            double h = gdk_pixbuf_get_height(cache->window_count) / (double)r->scale;
            quad(r, cache->window_count, r->style.right ? width - w : 0, center - h / 2, w, h,
                 white, arg->alpha, 0, center, 0, 0, 0, extent, 1);
        }
    } else {
        if (cache->window_count) {
            icon_renderer_forget(renderer, cache->window_count);
            g_clear_object(&cache->window_count);
        }
        guint count = arg->running_here ? arg->running : MIN(arg->running, 1);
        for (guint i = 0; i < count; i++) {
            double y = center + (i - (count - 1) / 2.0) * (bar_h + gap) - bar_h / 2;
            double x = r->style.right ? width - bar_w : 0;
            const double shadow[3] = {0, 0, 0};
            quad(r, r->textures[WHITE], x, y - 1, bar_w, bar_h + 2, shadow, .7 * arg->alpha, 0, y,
                 0, 0, 0, extent, 1);
            quad(r, r->textures[WHITE], x, y, bar_w, bar_h, marker_color,
                 (arg->running_here ? 1 : .45) * arg->alpha, 0, y, 0, 0, 0, extent, 1);
        }
    }
    if (arg->active)
        quad(r, r->textures[r->style.right ? ARROW : ACTIVE],
             r->style.right ? 0 : width - r->marker, center - round(r->marker / 2.0), r->marker,
             r->marker, marker_color, arg->alpha, 0, 0, 0, 0, 0, extent, 1);
}

/* Reuse the preblurred monochrome glow; only tint and blend at runtime.
 * Call after all icons: this final batch ignores the dock's hide transform. */
void icon_renderer_beacon(IconRenderer *renderer, double cy, const double color[3],
                          double intensity)
{
    Renderer *r = renderer->state;
    if (r->beacon_start == G_MAXUINT)
        r->beacon_start = r->vertices->len;
    cy = CLAMP(cy, 20, MAX(20, r->height - 20));
    double x = r->style.right ? r->content_width - 10 : 0;
    quad(r, r->textures[ATTENTION], x, cy - 22, 10, 44, color, intensity, x + 5, cy, 0, 0, 0,
         r->height, 1);
    x = r->style.right ? r->content_width - 2 : 0;
    quad(r, r->textures[WHITE], x, cy - 10, 2, 20, color, intensity, x + 1, cy, 0, 0, 0, r->height,
         1);
}

#ifdef LAUNCHER_RENDER_GL
void icon_renderer_end(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    gsize bytes = (gsize)r->vertices->len * sizeof(Vertex);
    if (!bytes)
        return;
    glUseProgram(r->program);
    double offset = r->style.hide_animation == HIDE_FADE
                        ? 0
                        : r->content_width * r->hide_progress * (r->style.right ? 1 : -1);
    double fade = r->style.hide_animation == HIDE_FADE         ? 1 - r->hide_progress
                  : r->style.hide_animation == HIDE_FADE_SLIDE ? 1 - .5 * r->hide_progress
                                                               : 1;
    glUniform4f(r->viewport_uniform, r->width, r->height, (float)offset, (float)fade);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, r->atlas);
    glBindVertexArray(r->vao);
    glBindBuffer(GL_ARRAY_BUFFER, r->vbo);
    if (bytes > r->buffer_capacity) {
        r->buffer_capacity = MAX(bytes, (gsize)2048 * sizeof(Vertex));
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)r->buffer_capacity, NULL, GL_DYNAMIC_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, r->vertices->data);
    guint normal = MIN(r->beacon_start, r->vertices->len);
    guint draws = 1;
    if (r->shelf_start <= normal) {
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)r->icons_start);
        int clip = (int)round(r->shelf_top * r->scale);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, r->height * r->scale - clip, r->width * r->scale, clip);
        glDrawArrays(GL_TRIANGLES, (GLint)r->icons_start,
                     (GLsizei)(r->shelf_start - r->icons_start));
        glDisable(GL_SCISSOR_TEST);
        glDrawArrays(GL_TRIANGLES, (GLint)r->shelf_start, (GLsizei)(normal - r->shelf_start));
        draws = 3;
    } else
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)normal);
    if (normal < r->vertices->len) {
        glUniform4f(r->viewport_uniform, r->width, r->height, 0, 1);
        glDrawArrays(GL_TRIANGLES, (GLint)normal, (GLsizei)(r->vertices->len - normal));
    }
    r->stats.draw_calls = draws + (normal < r->vertices->len ? 1 : 0);
    r->stats.vertices = r->vertices->len;
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

#else
void icon_renderer_end(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    if (!r->frame || cairo_surface_status(r->frame) != CAIRO_STATUS_SUCCESS)
        return;
    raster_frame(r);
    r->stats.draw_calls = 1;
    r->stats.vertices = r->vertices->len;
}

void icon_renderer_paint(IconRenderer *renderer, cairo_t *cr)
{
    Renderer *r = renderer->state;
    if (!r->frame || cairo_surface_status(r->frame) != CAIRO_STATUS_SUCCESS)
        return;
    cairo_save(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(cr, r->frame, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    cairo_restore(cr);
}

cairo_surface_t *icon_renderer_surface(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
    return r->frame;
}
#endif

RenderStats icon_renderer_stats(IconRenderer *renderer)
{
    Renderer *r = renderer->state;
#ifdef LAUNCHER_RENDER_GL
    r->stats.buffer_bytes = r->buffer_capacity;
#else
    r->stats.buffer_bytes = r->vertices->len * sizeof(Vertex);
    r->stats.image_bytes = r->image_bytes;
    r->stats.frame_bytes = r->frame ? (gsize)cairo_image_surface_get_stride(r->frame) *
                                          cairo_image_surface_get_height(r->frame)
                                    : 0;
#endif
    r->stats.cached_images = g_hash_table_size(r->sprites);
    return r->stats;
}

void icon_renderer_set_style(IconRenderer *renderer, const RenderStyle *style)
{
    Renderer *r = renderer->state;
    r->style = *style;
}
