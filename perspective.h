/* Planar perspective correction. GPL-2.0-or-later. */
#ifndef G3DATA2_PERSPECTIVE_H
#define G3DATA2_PERSPECTIVE_H
#include <glib.h>
#include <cairo.h>

typedef struct {
	gboolean valid, enabled;
	/* Original image pixels: top-left, top-right, bottom-right, bottom-left. */
	gdouble corners[4][2];
} PerspectiveState;

typedef struct {
	gdouble forward[9], inverse[9];
	gint width, height;
	/* Corrected pixels per source pixel at the selected rectangle's centre,
	 * using the square root of the local area scale (no preferred direction). */
	gdouble center_pixel_scale;
} PerspectiveTransform;

gboolean perspective_map(const gdouble matrix[9], gdouble x, gdouble y,
		gdouble *out_x, gdouble *out_y);
gboolean perspective_build(const PerspectiveState *state, gint width, gint height,
		PerspectiveTransform *transform, GError **error);
cairo_surface_t *perspective_render(cairo_surface_t *source, gdouble source_scale,
		const PerspectiveTransform *transform, GError **error);
#endif
