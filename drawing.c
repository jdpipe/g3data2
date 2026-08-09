/*

g3data2 : A program for grabbing data from scanned graphs
Copyright (C) 2011 Jonas Frantz

    This file is part of g3data2.

    g3data2 is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    g3data2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Authors email : jonas@frantz.fi

 */

#include <gtk/gtk.h>
#include "main.h"

/****************************************************************/
/* This function draws the X-axis, Y-axis or point marker on	*/
/* the drawing_area depending on the value of the type		*/
/* parameter.							*/
/****************************************************************/
void drawMarker(cairo_t *cr, gint x, gint y, gint type) {

	if (type == 0) {
		cairo_move_to(cr, x - MARKERLENGTH - 1, y);
		cairo_rel_line_to(cr, MARKERLENGTH * 2 + 1, 0);
		cairo_move_to(cr, x, y);
		cairo_rel_line_to(cr, 0, -MARKERLENGTH);
		cairo_set_source_rgb(cr, 0, 1, 0);
		cairo_stroke(cr);
	} else if (type == 1) {
		cairo_move_to(cr, x, y - MARKERLENGTH - 1);
		cairo_rel_line_to(cr, 0, MARKERLENGTH * 2 + 1);
		cairo_move_to(cr, x, y);
		cairo_rel_line_to(cr, MARKERLENGTH, 0);
		cairo_set_source_rgb(cr, 0, 0, 1);
		cairo_stroke(cr);
	} else {
		cairo_rectangle(cr, x - MARKERSIZE, y - MARKERSIZE, MARKERSIZE * 2 + 1,
				MARKERSIZE * 2 + 1);
		cairo_set_source_rgb(cr, 1, 0, 0);
		cairo_stroke(cr);
	}
}

void drawSeriesMarker(cairo_t *cr, gdouble x, gdouble y, guint32 rgba,
		gboolean active, gboolean hovered, gboolean selected) {
	gdouble red, green, blue, alpha;
	gdouble marker_size;

	rgba_to_components(rgba, &red, &green, &blue, &alpha);
	if (!active)
		alpha *= 0.55;
	marker_size = MARKERSIZE + (hovered ? 1.0 : 0.0);

	if (selected) {
		cairo_arc(cr, x, y, marker_size + 4.0, 0, 2.0 * G_PI);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.95);
		cairo_set_line_width(cr, 4.0);
		cairo_stroke(cr);
		cairo_arc(cr, x, y, marker_size + 4.0, 0, 2.0 * G_PI);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.95);
		cairo_set_line_width(cr, 1.5);
		cairo_stroke(cr);
	} else if (hovered) {
		cairo_arc(cr, x, y, marker_size + 3.0, 0, 2.0 * G_PI);
		cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.85);
		cairo_set_line_width(cr, 2.0);
		cairo_stroke(cr);
	}

	cairo_rectangle(cr, x - marker_size, y - marker_size,
			marker_size * 2.0 + 1.0, marker_size * 2.0 + 1.0);
	cairo_set_source_rgba(cr, red, green, blue, alpha);
	cairo_set_line_width(cr, active ? 2.0 : 1.5);
	cairo_stroke(cr);
}
