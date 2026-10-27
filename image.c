/* Image loading. GPL-2.0-or-later. */
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "image.h"

/* Keep conversion independent of a display connection and retain only the
 * Cairo buffer after loading. Cairo ARGB32 uses premultiplied, native-endian
 * pixels; GdkPixbuf uses unpremultiplied RGB/RGBA bytes. */
static cairo_surface_t *surface_from_pixbuf(GdkPixbuf *pixbuf) {
	gboolean alpha = gdk_pixbuf_get_has_alpha(pixbuf);
	gint width = gdk_pixbuf_get_width(pixbuf);
	gint height = gdk_pixbuf_get_height(pixbuf);
	gint source_stride = gdk_pixbuf_get_rowstride(pixbuf);
	gint channels = gdk_pixbuf_get_n_channels(pixbuf);
	const guchar *source = gdk_pixbuf_get_pixels(pixbuf);
	cairo_surface_t *surface = cairo_image_surface_create(
			alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, width, height);
	guchar *destination;
	gint destination_stride, x, y;

	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
		return surface;
	cairo_surface_flush(surface);
	destination = cairo_image_surface_get_data(surface);
	destination_stride = cairo_image_surface_get_stride(surface);
	for (y = 0; y < height; y++) {
		const guchar *pixel = source + (gsize) y * source_stride;
		guint32 *output = (guint32 *) (destination + (gsize) y * destination_stride);
		for (x = 0; x < width; x++, pixel += channels) {
			guint a = alpha ? pixel[3] : 255;
			guint r = (pixel[0] * a + 127) / 255;
			guint g = (pixel[1] * a + 127) / 255;
			guint b = (pixel[2] * a + 127) / 255;
			output[x] = (a << 24) | (r << 16) | (g << 8) | b;
		}
	}
	cairo_surface_mark_dirty(surface);
	return surface;
}

cairo_surface_t *load_image_surface(const gchar *filename, GError **error) {
	static const guchar png_signature[] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	guchar signature[sizeof(png_signature)];
	gsize bytes_read;
	FILE *file;
	cairo_surface_t *surface;
	cairo_status_t status;
	GdkPixbuf *pixbuf, *oriented;

	/* Inspect contents rather than trusting a possibly missing/wrong suffix. */
	file = g_fopen(filename, "rb");
	if (file == NULL) {
		gint saved_errno = errno;
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
				"Could not open image: %s", g_strerror(saved_errno));
		return NULL;
	}
	bytes_read = fread(signature, 1, sizeof(signature), file);
	if (ferror(file)) {
		gint saved_errno = errno;
		fclose(file);
		g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
				"Could not read image: %s", g_strerror(saved_errno));
		return NULL;
	}
	fclose(file);

	if (bytes_read == sizeof(signature)
			&& memcmp(signature, png_signature, sizeof(signature)) == 0) {
		/* Keep the direct PNG path and its lower peak memory usage. */
		surface = cairo_image_surface_create_from_png(filename);
	} else {
		pixbuf = gdk_pixbuf_new_from_file(filename, error);
		if (pixbuf == NULL)
			return NULL;
		oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf);
		g_object_unref(pixbuf);
		if (oriented == NULL) {
			g_set_error_literal(error, GDK_PIXBUF_ERROR,
					GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY,
					"Could not apply the image's orientation");
			return NULL;
		}
		surface = surface_from_pixbuf(oriented);
		g_object_unref(oriented);
	}
	status = cairo_surface_status(surface);
	if (status == CAIRO_STATUS_SUCCESS)
		return surface;

	g_set_error(error, GDK_PIXBUF_ERROR,
			status == CAIRO_STATUS_NO_MEMORY ? GDK_PIXBUF_ERROR_INSUFFICIENT_MEMORY
					: GDK_PIXBUF_ERROR_FAILED,
			"Could not create image surface: %s", cairo_status_to_string(status));
	cairo_surface_destroy(surface);
	return NULL;
}
