/* Image loading shared by the application and its tests. GPL-2.0-or-later. */
#ifndef G3DATA2_IMAGE_H
#define G3DATA2_IMAGE_H

#include <cairo.h>
#include <glib.h>

/* Returns an owned image surface, or NULL with an error. Non-PNG images
 * use their embedded orientation; PNG coordinates retain their old meaning. */
cairo_surface_t *load_image_surface(const gchar *filename, GError **error);

#endif
