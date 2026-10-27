/* Planar perspective correction. GPL-2.0-or-later. */
#include "perspective.h"
#include <math.h>
#include <string.h>

static gboolean fail(GError **error, const gchar *message) {
	g_set_error_literal(error, g_quark_from_static_string("g3data2-perspective"),
			1, message);
	return FALSE;
}

gboolean perspective_map(const gdouble m[9], gdouble x, gdouble y,
		gdouble *out_x, gdouble *out_y) {
	gdouble d = m[6] * x + m[7] * y + m[8];
	if (!isfinite(d) || fabs(d) < 1e-12)
		return FALSE;
	*out_x = (m[0] * x + m[1] * y + m[2]) / d;
	*out_y = (m[3] * x + m[4] * y + m[5]) / d;
	return isfinite(*out_x) && isfinite(*out_y);
}

static gboolean invert(const gdouble m[9], gdouble result[9]) {
	gdouble a[9], det;
	gint i;
	a[0] = m[4]*m[8] - m[5]*m[7];
	a[1] = m[2]*m[7] - m[1]*m[8];
	a[2] = m[1]*m[5] - m[2]*m[4];
	a[3] = m[5]*m[6] - m[3]*m[8];
	a[4] = m[0]*m[8] - m[2]*m[6];
	a[5] = m[2]*m[3] - m[0]*m[5];
	a[6] = m[3]*m[7] - m[4]*m[6];
	a[7] = m[1]*m[6] - m[0]*m[7];
	a[8] = m[0]*m[4] - m[1]*m[3];
	det = m[0]*a[0] + m[1]*a[3] + m[2]*a[6];
	if (!isfinite(det) || fabs(det) < 1e-15)
		return FALSE;
	for (i = 0; i < 9; i++)
		result[i] = a[i] / det;
	return TRUE;
}

gboolean perspective_build(const PerspectiveState *state, gint width, gint height,
		PerspectiveTransform *transform, GError **error) {
	gdouble p[4][2], system[8][9] = {{0}}, h[9];
	const gdouble target[4][2] = {{0,0},{1,0},{1,1},{0,1}};
	gdouble rw, rh, left = G_MAXDOUBLE, top = G_MAXDOUBLE;
	gdouble right = -G_MAXDOUBLE, bottom = -G_MAXDOUBLE, denominator = 0;
	gdouble image_corners[4][2] = {{0,0},{width-1,0},
		{width-1,height-1},{0,height-1}};
	gdouble scale, output_width, output_height;
	gint i, j, k, pivot;
	if (!state->valid || width < 2 || height < 2)
		return fail(error, "Select all four corners first.");
	for (i = 0; i < 4; i++) {
		for (j = 0; j < 2; j++) {
			gdouble value = state->corners[i][j];
			if (!isfinite(value) || value < 0 || value > (j ? height-1 : width-1))
				return fail(error, "All corners must lie inside the original image.");
			p[i][j] = value / (j ? height-1 : width-1);
		}
	}
	for (i = 0; i < 4; i++) {
		gint b = (i+1)%4, c = (i+2)%4;
		gdouble cross = (p[b][0]-p[i][0])*(p[c][1]-p[b][1])
				- (p[b][1]-p[i][1])*(p[c][0]-p[b][0]);
		if (cross <= 1e-7)
			return fail(error, "Select a convex rectangle in order: top-left, top-right, bottom-right, bottom-left. Corners must be distinct and well separated.");
	}
	/* Solve in normalized image coordinates to avoid pixel-scale conditioning. */
	for (i = 0; i < 4; i++) {
		gdouble x = p[i][0], y = p[i][1], u = target[i][0], v = target[i][1];
		gdouble row_x[9] = {x,y,1,0,0,0,-u*x,-u*y,u};
		gdouble row_y[9] = {0,0,0,x,y,1,-v*x,-v*y,v};
		memcpy(system[2*i], row_x, sizeof(row_x));
		memcpy(system[2*i+1], row_y, sizeof(row_y));
	}
	for (i = 0; i < 8; i++) {
		pivot = i;
		for (j = i+1; j < 8; j++)
			if (fabs(system[j][i]) > fabs(system[pivot][i])) pivot = j;
		if (fabs(system[pivot][i]) < 1e-12)
			return fail(error, "These corners do not define a stable correction.");
		for (k = i; k < 9; k++) {
			gdouble temp = system[i][k];
			system[i][k] = system[pivot][k]; system[pivot][k] = temp;
		}
		scale = system[i][i];
		for (k = i; k < 9; k++) system[i][k] /= scale;
		for (j = 0; j < 8; j++) if (j != i) {
			scale = system[j][i];
			for (k = i; k < 9; k++) system[j][k] -= scale * system[i][k];
		}
	}
	for (i = 0; i < 8; i++) h[i] = system[i][8];
	h[8] = 1;
	/* Use the mean opposite-edge lengths for a useful preview aspect ratio. */
	rw = (hypot(state->corners[1][0]-state->corners[0][0], state->corners[1][1]-state->corners[0][1])
			+ hypot(state->corners[2][0]-state->corners[3][0], state->corners[2][1]-state->corners[3][1])) / 2;
	rh = (hypot(state->corners[3][0]-state->corners[0][0], state->corners[3][1]-state->corners[0][1])
			+ hypot(state->corners[2][0]-state->corners[1][0], state->corners[2][1]-state->corners[1][1])) / 2;
	for (i = 0; i < 9; i++) {
		if (i%3 == 0) h[i] /= width-1;
		if (i%3 == 1) h[i] /= height-1;
		if (i < 3) h[i] *= rw;
		else if (i < 6) h[i] *= rh;
	}
	/* Bound the WHOLE image, preserving labels and margins outside the plot. */
	for (i = 0; i < 4; i++) {
		gdouble x = image_corners[i][0], y = image_corners[i][1], u, v;
		gdouble d = h[6]*x + h[7]*y + h[8];
		if (fabs(d) < 1e-7 || (i && d * denominator <= 0)
				|| !perspective_map(h,x,y,&u,&v))
			return fail(error, "This correction would stretch part of the image to infinity. Choose a less extreme rectangle.");
		denominator = d;
		left = MIN(left,u); right = MAX(right,u);
		top = MIN(top,v); bottom = MAX(bottom,v);
	}
	left = floor(left); top = floor(top);
	output_width = ceil(right) - left + 1;
	output_height = ceil(bottom) - top + 1;
	/* Bound the cached bitmap, independently of zoom and stored coordinates. */
	scale = MIN(1.0, MIN(16000.0/output_width, 16000.0/output_height));
	scale = MIN(scale, sqrt(32000000.0/(output_width*output_height)));
	if (!isfinite(scale) || scale <= 0)
		return fail(error, "The corrected image is too large.");
	for (i = 0; i < 3; i++) {
		h[i] = (h[i] - left*h[i+6])*scale;
		h[i+3] = (h[i+3] - top*h[i+6])*scale;
	}
	transform->width = MAX(1, (gint) ceil(output_width*scale));
	transform->height = MAX(1, (gint) ceil(output_height*scale));
	memcpy(transform->forward,h,sizeof(h));
	if (!invert(h,transform->inverse))
		return fail(error, "These corners do not define an invertible correction.");
	{
		gdouble left_x, top_y, right_x, bottom_y, x, y, u, v, d;
		gdouble jxx, jxy, jyx, jyy;
		/* Use the centre of the corrected rectangle, not the arithmetic mean
		 * of its photographed corners or the centre of the padded bitmap. */
		if (!perspective_map(h,state->corners[0][0],state->corners[0][1],&left_x,&top_y)
				|| !perspective_map(h,state->corners[2][0],state->corners[2][1],&right_x,&bottom_y))
			return fail(error, "Could not determine the rectangle's centre.");
		u = (left_x + right_x) / 2;
		v = (top_y + bottom_y) / 2;
		if (!perspective_map(transform->inverse,u,v,&x,&y))
			return fail(error, "Could not determine the rectangle's centre.");
		d = h[6]*x + h[7]*y + h[8];
		jxx = (h[0] - u*h[6]) / d;
		jxy = (h[1] - u*h[7]) / d;
		jyx = (h[3] - v*h[6]) / d;
		jyy = (h[4] - v*h[7]) / d;
		transform->center_pixel_scale = sqrt(fabs(jxx*jyy - jxy*jyx));
		if (!isfinite(transform->center_pixel_scale) || transform->center_pixel_scale <= 0)
			return fail(error, "Could not determine the pixel scale at the rectangle's centre.");
	}
	return TRUE;
}

cairo_surface_t *perspective_render(cairo_surface_t *source, gdouble source_scale,
		const PerspectiveTransform *t, GError **error) {
	cairo_surface_t *result = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,t->width,t->height);
	gint x,y,sw = cairo_image_surface_get_width(source), sh = cairo_image_surface_get_height(source);
	gint ss = cairo_image_surface_get_stride(source), ds;
	guchar *src, *dst;
	gboolean opaque = cairo_image_surface_get_format(source) == CAIRO_FORMAT_RGB24;
	if (cairo_surface_status(result) != CAIRO_STATUS_SUCCESS) {
		fail(error, cairo_status_to_string(cairo_surface_status(result)));
		cairo_surface_destroy(result);
		return NULL;
	}
	cairo_surface_flush(source); cairo_surface_flush(result);
	src = cairo_image_surface_get_data(source); dst = cairo_image_surface_get_data(result);
	ds = cairo_image_surface_get_stride(result);
	/* Inverse mapping avoids holes; interpolate premultiplied colour/alpha.
	 * The new surface starts transparent outside the transformed footprint. */
	for (y=0;y<t->height;y++) {
		guint32 *row = (guint32 *)(dst+(gsize)y*ds);
		for (x=0;x<t->width;x++) {
			gdouble sx,sy,fx,fy;
			gint ix,iy,k;
			guint32 pixels[4], value=0;
			gdouble weights[4];
			if (!perspective_map(t->inverse,x,y,&sx,&sy)) continue;
			sx *= source_scale; sy *= source_scale;
			if (sx < -1e-7 || sy < -1e-7 || sx > sw-1+1e-7 || sy > sh-1+1e-7) continue;
			sx = CLAMP(sx,0,sw-1); sy = CLAMP(sy,0,sh-1);
			ix = (gint)floor(sx); iy = (gint)floor(sy); fx=sx-ix; fy=sy-iy;
			pixels[0] = ((guint32 *)(src+(gsize)iy*ss))[ix];
			pixels[1] = ((guint32 *)(src+(gsize)iy*ss))[MIN(ix+1,sw-1)];
			pixels[2] = ((guint32 *)(src+(gsize)MIN(iy+1,sh-1)*ss))[ix];
			pixels[3] = ((guint32 *)(src+(gsize)MIN(iy+1,sh-1)*ss))[MIN(ix+1,sw-1)];
			weights[0]=(1-fx)*(1-fy); weights[1]=fx*(1-fy); weights[2]=(1-fx)*fy; weights[3]=fx*fy;
			for (k=0;k<4;k++) {
				gint shift=k*8, n;
				gdouble channel=0;
				for (n=0;n<4;n++) channel += weights[n]*((pixels[n]>>shift)&255);
				value |= (guint32)(channel+0.5)<<shift;
			}
			row[x] = opaque ? value|0xff000000u : value;
		}
	}
	cairo_surface_mark_dirty(result);
	return result;
}
