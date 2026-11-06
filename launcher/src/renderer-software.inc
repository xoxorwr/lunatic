/* SPDX-License-Identifier: GPL-3.0-only
 * CPU backend for the same Unity projection and layer commands as the GL port.
 * No GL context, driver worker pool, texture atlas or per-frame image decoding.
 */
static Sprite *ensure_image(Renderer *r, GdkPixbuf *image)
{
    if (!image)
        return NULL;
    Sprite *sprite = g_hash_table_lookup(r->sprites, image);
    if (sprite)
        return sprite;
    int w = gdk_pixbuf_get_width(image), h = gdk_pixbuf_get_height(image);
    GdkPixbuf *scaled = NULL;
    if (MAX(w, h) > MAX(r->glow_size, r->tile) * r->scale) {
        double ratio = (double)(r->image * r->scale) / MAX(w, h);
        w = MAX(1, (int)round(w * ratio));
        h = MAX(1, (int)round(h * ratio));
        scaled = gdk_pixbuf_scale_simple(image, w, h, GDK_INTERP_BILINEAR);
        if (!scaled)
            return NULL;
    }
    GdkPixbuf *pixels = scaled ? scaled : image;
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w + 4, h + 4);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_clear_object(&scaled);
        return NULL;
    }
    guint32 *out = (guint32 *)cairo_image_surface_get_data(surface);
    int out_stride = cairo_image_surface_get_stride(surface) / 4;
    int stride = gdk_pixbuf_get_rowstride(pixels), channels = gdk_pixbuf_get_n_channels(pixels);
    const guchar *data = gdk_pixbuf_read_pixels(pixels);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const guchar *in = data + y * stride + x * channels;
            guint a = channels == 4 ? in[3] : 255;
            out[(y + 2) * out_stride + x + 2] = (a << 24) | (((in[0] * a + 127) / 255) << 16) |
                                                (((in[1] * a + 127) / 255) << 8) |
                                                ((in[2] * a + 127) / 255);
        }
    cairo_surface_mark_dirty(surface);
    g_clear_object(&scaled);
    sprite = g_new0(Sprite, 1);
    *sprite = (Sprite){.x = 2, .y = 2, .w = w, .h = h, .surface = surface};
    r->image_bytes += (gsize)out_stride * (h + 4) * 4;
    g_hash_table_insert(r->sprites, g_object_ref(image), sprite);
    r->stats.image_updates++; /* Cold image conversions; shared stats with GL. */
    return sprite;
}

static gboolean prepare_frame(Renderer *r)
{
    int width = r->width * r->scale, height = r->height * r->scale;
    if (r->frame && (cairo_image_surface_get_width(r->frame) != width ||
                     cairo_image_surface_get_height(r->frame) != height)) {
        cairo_surface_destroy(r->frame);
        r->frame = NULL;
    }
    if (!r->frame) {
        r->frame = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
        cairo_surface_set_device_scale(r->frame, r->scale, r->scale);
    }
    if (cairo_surface_status(r->frame) != CAIRO_STATUS_SUCCESS) {
        g_warning("Cannot allocate launcher frame: %s",
                  cairo_status_to_string(cairo_surface_status(r->frame)));
        return FALSE;
    }
    cairo_surface_flush(r->frame);
    memset(cairo_image_surface_get_data(r->frame), 0,
           (gsize)cairo_image_surface_get_stride(r->frame) * height);
    return TRUE;
}

/* Interpolate premultiplied channels together in pairs, without allocations. */
static inline guint32 lerp_pixel(guint32 a, guint32 b, guint fraction)
{
    const guint32 mask = 0x00ff00ff;
    guint32 rb = (((a & mask) * (256 - fraction) + (b & mask) * fraction + 0x00800080) >> 8) & mask;
    guint32 ag =
        (((((a >> 8) & mask) * (256 - fraction) + ((b >> 8) & mask) * fraction + 0x00800080) >> 8) &
         mask)
        << 8;
    return rb | ag;
}

static inline guint32 sample_pixel(const guint32 *pixels, int stride, int width, int height,
                                   double u, double v)
{
    u = CLAMP(u - .5, 0, width - 1);
    v = CLAMP(v - .5, 0, height - 1);
    int x = (int)u, y = (int)v;
    int next_x = MIN(x + 1, width - 1), next_y = MIN(y + 1, height - 1);
    guint fx = (guint)((u - x) * 256), fy = (guint)((v - y) * 256);
    guint32 top = lerp_pixel(pixels[y * stride + x], pixels[y * stride + next_x], fx);
    guint32 bottom = lerp_pixel(pixels[next_y * stride + x], pixels[next_y * stride + next_x], fx);
    return lerp_pixel(top, bottom, fy);
}

static inline guint32 blend_pixel(guint32 source, guint32 destination, const guint tint[4])
{
    guint a = MIN(255u, (((source >> 24) * tint[3] + 32768) >> 16));
    if (!a)
        return destination;
    guint inverse = 255 - a;
    guint out = MIN(255u, a + (((destination >> 24) * inverse + 127) / 255)) << 24;
    for (int channel = 0; channel < 3; channel++) {
        int shift = 16 - channel * 8;
        guint value = ((((source >> shift) & 255) * tint[channel] + 32768) >> 16) +
                      ((((destination >> shift) & 255) * inverse + 127) / 255);
        out |= MIN(255u, value) << shift;
    }
    return out;
}

static inline guint32 blend_solid(guint32 source, guint32 destination)
{
    guint inverse = 255 - (source >> 24);
    const guint32 mask = 0x00ff00ff;
    guint32 rb = (destination & mask) * inverse + 0x00800080;
    guint32 ag = ((destination >> 8) & mask) * inverse + 0x00800080;
    rb = ((rb + ((rb >> 8) & mask)) >> 8) & mask;
    ag = ((ag + ((ag >> 8) & mask)) >> 8) & mask;
    return source + rb + (ag << 8);
}

/* A projected planar quad has affine u/w, v/w and 1/w. Interpolate those
 * quantities across scanlines, then divide, exactly as the original GL shader.
 * Rasterizing the whole convex quad avoids diagonal seams or double blending. */
static void raster_quad(Renderer *r, const Vertex *vertices, cairo_surface_t *source, double offset,
                        double fade, double clip_bottom)
{
    const int corners[] = {0, 1, 2, 5};
    double x[4], y[4], coverage_x[4], coverage_y[4], uq[3], vq[3], q[3];
    double min_y = G_MAXDOUBLE, max_y = -G_MAXDOUBLE;
    for (int i = 0; i < 4; i++) {
        const Vertex *v = &vertices[corners[i]];
        x[i] = (v->x + offset) * r->scale;
        y[i] = v->y * r->scale;
        /* GL raster coverage snaps vertices to a device subpixel grid while
         * interpolation retains the projected coordinates. Keep half-pixel
         * boundaries stable despite float projection roundoff. */
        coverage_x[i] = round(x[i] * 256) / 256;
        coverage_y[i] = round(y[i] * 256) / 256;
        min_y = MIN(min_y, coverage_y[i]);
        max_y = MAX(max_y, coverage_y[i]);
        if (i < 3) {
            q[i] = 1.0 / v->w;
            uq[i] = v->u * q[i];
            vq[i] = v->v * q[i];
        }
    }
    double determinant = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    if (!isfinite(determinant) || fabs(determinant) < 1e-9)
        return;
    double dx[3], dy[3], origin[3];
    const double *values[] = {uq, vq, q};
    for (int i = 0; i < 3; i++) {
        const double *v = values[i];
        dx[i] = ((v[1] - v[0]) * (y[2] - y[0]) - (v[2] - v[0]) * (y[1] - y[0])) / determinant;
        dy[i] = ((x[1] - x[0]) * (v[2] - v[0]) - (x[2] - x[0]) * (v[1] - v[0])) / determinant;
        origin[i] = v[0] - dx[i] * x[0] - dy[i] * y[0];
    }
    guint tint[4];
    const Vertex *v = vertices;
    const double color[] = {v->r, v->g, v->b, 1};
    for (int i = 0; i < 4; i++)
        tint[i] = (guint)round(CLAMP(color[i] * v->a * fade, 0, 16) * 65536);
    if (!tint[3])
        return;
    int width = r->width * r->scale, height = r->height * r->scale;
    int top = MAX(0, (int)floor(min_y + .5)), bottom = MIN(height, (int)floor(max_y + .5));
    bottom = MIN(bottom, (int)round(clip_bottom * r->scale));
    guint32 *output = (guint32 *)cairo_image_surface_get_data(r->frame);
    int output_stride = cairo_image_surface_get_stride(r->frame) / 4;
    const guint32 *pixels = (const guint32 *)cairo_image_surface_get_data(source);
    int stride = cairo_image_surface_get_stride(source) / 4;
    int sw = cairo_image_surface_get_width(source), sh = cairo_image_surface_get_height(source);
    gboolean solid = sw == 5 && sh == 5; /* The 1×1 constant-white sprite. */
    guint32 solid_pixel = solid ? blend_pixel(pixels[2 * stride + 2], 0, tint) : 0;
    for (int row = top; row < bottom; row++) {
        double scan = row + .5, left = G_MAXDOUBLE, right = -G_MAXDOUBLE;
        for (int edge = 0; edge < 4; edge++) {
            int next = (edge + 1) % 4;
            if (scan <= MIN(coverage_y[edge], coverage_y[next]) ||
                scan > MAX(coverage_y[edge], coverage_y[next]))
                continue;
            double crossing = coverage_x[edge] + (scan - coverage_y[edge]) *
                                                     (coverage_x[next] - coverage_x[edge]) /
                                                     (coverage_y[next] - coverage_y[edge]);
            left = MIN(left, crossing);
            right = MAX(right, crossing);
        }
        if (left > right)
            continue;
        int start = MAX(0, (int)ceil(left - .5)), end = MIN(width, (int)ceil(right - .5));
        double u = origin[0] + dx[0] * (start + .5) + dy[0] * scan;
        double t = origin[1] + dx[1] * (start + .5) + dy[1] * scan;
        double reciprocal = origin[2] + dx[2] * (start + .5) + dy[2] * scan;
        double inverse = fabs(dx[2]) < 1e-12 ? 1 / reciprocal : 0;
        guint32 *out = output + row * output_stride;
        if (solid) {
            for (int column = start; column < end; column++)
                out[column] = blend_solid(solid_pixel, out[column]);
            continue;
        }
        for (int column = start; column < end; column++) {
            double w = inverse ? inverse : 1 / reciprocal;
            guint32 sample = sample_pixel(pixels, stride, sw, sh, u * w, t * w);
            out[column] = blend_pixel(sample, out[column], tint);
            u += dx[0];
            t += dx[1];
            reciprocal += dx[2];
        }
    }
}

static void raster_frame(Renderer *r)
{
    double offset = r->style.hide_animation == HIDE_FADE
                        ? 0
                        : r->content_width * r->hide_progress * (r->style.right ? 1 : -1);
    double fade = r->style.hide_animation == HIDE_FADE         ? 1 - r->hide_progress
                  : r->style.hide_animation == HIDE_FADE_SLIDE ? 1 - .5 * r->hide_progress
                                                               : 1;
    for (guint i = 0; i < r->sources->len; i++)
        raster_quad(r, &g_array_index(r->vertices, Vertex, i * 6), g_ptr_array_index(r->sources, i),
                    i * 6 >= r->beacon_start ? 0 : offset, i * 6 >= r->beacon_start ? 1 : fade,
                    i * 6 >= r->icons_start && i * 6 < r->shelf_start ? r->shelf_top : r->height);
    cairo_surface_mark_dirty(r->frame);
    /* Queued commands must not retain images forgotten after the frame. */
    g_ptr_array_set_size(r->sources, 0);
}
