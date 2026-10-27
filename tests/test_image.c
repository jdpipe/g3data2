/* Image loading regression tests. GPL-2.0-or-later. */
#include <CUnit/Basic.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>

#include "../image.h"

static gboolean has_format(const gchar *name, gboolean writable) {
	GSList *formats = gdk_pixbuf_get_formats(), *item;
	gboolean found = FALSE;
	for (item = formats; item != NULL; item = item->next) {
		gchar *format_name = gdk_pixbuf_format_get_name(item->data);
		if (strcmp(name, format_name) == 0
				&& !gdk_pixbuf_format_is_disabled(item->data)
				&& (!writable || gdk_pixbuf_format_is_writable(item->data)))
			found = TRUE;
		g_free(format_name);
	}
	g_slist_free(formats);
	return found;
}

static void check_image(const gchar *path, gint width, gint height,
		gboolean transparent) {
	GError *error = NULL;
	cairo_surface_t *surface = load_image_surface(path, &error);
	CU_ASSERT_PTR_NULL(error);
	g_clear_error(&error);
	CU_ASSERT_PTR_NOT_NULL_FATAL(surface);
	CU_ASSERT_EQUAL(cairo_surface_get_type(surface), CAIRO_SURFACE_TYPE_IMAGE);
	CU_ASSERT_EQUAL(cairo_image_surface_get_width(surface), width);
	CU_ASSERT_EQUAL(cairo_image_surface_get_height(surface), height);
	if (transparent) {
		guint32 pixel;
		cairo_surface_flush(surface);
		memcpy(&pixel, cairo_image_surface_get_data(surface), sizeof(pixel));
		CU_ASSERT_EQUAL(cairo_image_surface_get_format(surface), CAIRO_FORMAT_ARGB32);
		CU_ASSERT_EQUAL(pixel >> 24, 0);
	}
	cairo_surface_destroy(surface);
}

void test_image_loading(void) {
	const gchar *formats[] = { "png", "jpeg", "bmp", "tiff" };
	/* A transparent GIF exercises a non-PNG alpha channel. */
	const gchar *gif = "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";
	const gchar *webp = "UklGRhoAAABXRUJQVlA4TA0AAAAvAYAAEAcQERGIiP4HAA==";
	const gchar *webp_alpha = "UklGRh4AAABXRUJQVlA4TBEAAAAvAYAAEAdQoAIWpICBiOh/AAA=";
	/* EXIF orientation 6: rotate a 3x2 JPEG clockwise to 2x3. */
	const guchar exif[] = {
		0xff, 0xe1, 0x00, 0x22, 'E', 'x', 'i', 'f', 0, 0,
		'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0,
		0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0
	};
	GError *error = NULL;
	gchar *directory = g_dir_make_tmp("g3data2-images-XXXXXX", &error);
	gchar *path;
	GdkPixbuf *pixbuf;
	guint i;
	guchar *data;
	gsize length;
	cairo_surface_t *surface;

	CU_ASSERT_PTR_NULL(error);
	CU_ASSERT_PTR_NOT_NULL_FATAL(directory);
	/* Deliberately use a misleading suffix to check content detection. */
	path = g_build_filename(directory, "image.JPEG", NULL);
	pixbuf = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 3, 2);
	gdk_pixbuf_fill(pixbuf, 0xff000000);
	for (i = 0; i < G_N_ELEMENTS(formats); i++) {
		if (!has_format(formats[i], TRUE)) {
			g_print("Skipping %s fixture: encoder unavailable\n", formats[i]);
			continue;
		}
		CU_ASSERT_TRUE(gdk_pixbuf_save(pixbuf, path, formats[i], &error, NULL));
		CU_ASSERT_PTR_NULL(error);
		g_clear_error(&error);
		check_image(path, 3, 2, strcmp(formats[i], "png") == 0
				|| strcmp(formats[i], "tiff") == 0);
		if (strcmp(formats[i], "jpeg") == 0) {
			gchar *jpeg;
			GByteArray *rotated = g_byte_array_new();
			CU_ASSERT_TRUE_FATAL(g_file_get_contents(path, &jpeg, &length, NULL));
			g_byte_array_append(rotated, (guchar *) jpeg, 2);
			g_byte_array_append(rotated, exif, sizeof(exif));
			g_byte_array_append(rotated, (guchar *) jpeg + 2, length - 2);
			CU_ASSERT_TRUE(g_file_set_contents(path, (gchar *) rotated->data,
					rotated->len, NULL));
			check_image(path, 2, 3, FALSE);
			g_byte_array_unref(rotated);
			g_free(jpeg);
		}
	}
	g_object_unref(pixbuf);
	/* A lossless RGBA fixture checks channel order and premultiplied alpha. */
	if (has_format("webp", FALSE)) {
		guint32 pixel;
		data = g_base64_decode(webp_alpha, &length);
		CU_ASSERT_TRUE(g_file_set_contents(path, (gchar *) data, length, NULL));
		g_free(data);
		surface = load_image_surface(path, &error);
		CU_ASSERT_PTR_NULL(error);
		CU_ASSERT_PTR_NOT_NULL_FATAL(surface);
		cairo_surface_flush(surface);
		memcpy(&pixel, cairo_image_surface_get_data(surface), sizeof(pixel));
		CU_ASSERT_EQUAL(pixel, 0x80402010);
		cairo_surface_destroy(surface);
	}
	if (has_format("gif", FALSE)) {
		data = g_base64_decode(gif, &length);
		CU_ASSERT_TRUE(g_file_set_contents(path, (gchar *) data, length, NULL));
		check_image(path, 1, 1, TRUE);
		g_free(data);
	}
	if (has_format("webp", FALSE)) {
		data = g_base64_decode(webp, &length);
		CU_ASSERT_TRUE(g_file_set_contents(path, (gchar *) data, length, NULL));
		check_image(path, 2, 3, TRUE);
		g_free(data);
	} else {
		g_print("Skipping WebP fixture: loader unavailable\n");
	}
	/* Both PNG and generic decoder failures must return an actionable error. */
	CU_ASSERT_TRUE(g_file_set_contents(path, "\211PNG\r\n\032\n", 8, NULL));
	surface = load_image_surface(path, &error);
	CU_ASSERT_PTR_NULL(surface);
	CU_ASSERT_PTR_NOT_NULL(error);
	g_clear_error(&error);
	CU_ASSERT_TRUE(g_file_set_contents(path, "not an image", -1, NULL));
	surface = load_image_surface(path, &error);
	CU_ASSERT_PTR_NULL(surface);
	CU_ASSERT_PTR_NOT_NULL(error);
	g_clear_error(&error);
	g_unlink(path);
	surface = load_image_surface(path, &error);
	CU_ASSERT_PTR_NULL(surface);
	CU_ASSERT_TRUE(error != NULL && error->domain == G_FILE_ERROR
			&& error->code == G_FILE_ERROR_NOENT);
	g_clear_error(&error);
	g_free(path);
	g_rmdir(directory);
	g_free(directory);
}
