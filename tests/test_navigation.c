#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

#include "../main.h"

gint addImageToTab(GtkWidget *alignment, char *filename, gdouble scale,
		gdouble max_x, gdouble max_y, struct TabData *tab);

typedef struct {
	struct TabData tab;
	GtkWidget *window;
	GtkAdjustment *horizontal, *vertical;
} NavigationFixture;

static void setup(NavigationFixture *fixture, gconstpointer data) {
	GtkWidget *alignment, *scrolled;
	cairo_surface_t *image;
	gchar *path;
	gint fd;
	(void) data;

	fd = g_file_open_tmp("g3data2-navigation-XXXXXX.png", &path, NULL);
	g_assert_cmpint(fd, >=, 0);
	close(fd);
	image = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1000, 800);
	g_assert_cmpint(cairo_surface_write_to_png(image, path), ==,
			CAIRO_STATUS_SUCCESS);
	cairo_surface_destroy(image);

	fixture->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(fixture->window), 400, 300);
	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(fixture->window), scrolled);
	fixture->tab.ViewPort = gtk_viewport_new(NULL, NULL);
	gtk_container_add(GTK_CONTAINER(scrolled), fixture->tab.ViewPort);
	alignment = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(fixture->tab.ViewPort), alignment);
	fixture->tab.document = image_document_new(0);
	image_document_add_series(fixture->tab.document, "Test", G3_COLOR_RED);
	fixture->tab.history = history_new(&fixture->tab);
	fixture->tab.viewZoom = 1.0;
	fixture->tab.zoom_area = g_object_ref_sink(gtk_drawing_area_new());
	g_assert_cmpint(addImageToTab(alignment, path, -1, -1, -1,
			&fixture->tab), ==, 0);
	g_unlink(path);
	g_free(path);
	/* Use a known zoom and scroll position rather than the initial fit idle. */
	g_assert_true(g_source_remove_by_user_data(&fixture->tab));
	fixture->tab.zoomedToFit = FALSE;
	gtk_widget_show_all(fixture->window);
	while (gtk_events_pending())
		gtk_main_iteration();
	fixture->horizontal = gtk_scrollable_get_hadjustment(
			GTK_SCROLLABLE(fixture->tab.ViewPort));
	fixture->vertical = gtk_scrollable_get_vadjustment(
			GTK_SCROLLABLE(fixture->tab.ViewPort));
	gtk_adjustment_configure(fixture->horizontal, 600, 0, 2024, 10, 100, 400);
	gtk_adjustment_configure(fixture->vertical, 500, 0, 1824, 10, 100, 300);
	fixture->tab.zoomedToFit = TRUE;
	g_assert_true((gtk_widget_get_events(fixture->tab.drawing_area)
			& GDK_SMOOTH_SCROLL_MASK) != 0);
}

static void teardown(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	gtk_widget_destroy(fixture->window);
	gtk_widget_destroy(fixture->tab.zoom_area);
	g_object_unref(fixture->tab.zoom_area);
	cairo_surface_destroy(fixture->tab.originalImage);
	cairo_surface_destroy(fixture->tab.image);
	history_free(fixture->tab.history);
	image_document_free(fixture->tab.document);
}

static void scroll_at_time(NavigationFixture *fixture, GdkScrollDirection direction,
		guint state, gdouble dx, gdouble dy, gdouble x, gdouble y, guint32 time) {
	GdkEventScroll event = { 0 };
	gboolean handled = FALSE;
	event.type = GDK_SCROLL;
	event.direction = direction;
	event.state = state;
	event.time = time;
	event.delta_x = dx;
	event.delta_y = dy;
	/* A fixed position in the viewport, expressed in drawing-area coordinates. */
	event.x = gtk_adjustment_get_value(fixture->horizontal) + x;
	event.y = gtk_adjustment_get_value(fixture->vertical) + y;
	g_signal_emit_by_name(fixture->tab.drawing_area, "scroll-event", &event,
			&handled);
	g_assert_true(handled);
}

static void scroll_at(NavigationFixture *fixture, GdkScrollDirection direction,
		guint state, gdouble dx, gdouble dy, gdouble x, gdouble y) {
	scroll_at_time(fixture, direction, state, dx, dy, x, y, 0);
}

static void scroll(NavigationFixture *fixture, GdkScrollDirection direction,
		guint state, gdouble dx, gdouble dy) {
	scroll_at(fixture, direction, state, dx, dy, 100, 150);
}

static void assert_anchor(NavigationFixture *fixture, gdouble image_x,
		gdouble image_y, gdouble screen_x, gdouble screen_y) {
	gdouble h = fixture->tab.pendingZoomScrollOnAdjust
			? fixture->tab.pendingZoomScrollTarget[0]
			: gtk_adjustment_get_value(fixture->horizontal);
	gdouble v = fixture->tab.pendingZoomScrollOnAdjust
			? fixture->tab.pendingZoomScrollTarget[1]
			: gtk_adjustment_get_value(fixture->vertical);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewOrigin[0]
			+ image_x * fixture->tab.viewZoom - h, screen_x, 1e-7);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewOrigin[1]
			+ image_y * fixture->tab.viewZoom - v, screen_y, 1e-7);
}

static void test_extreme_zoom(NavigationFixture *fixture, gconstpointer data) {
	GtkAllocation allocation = { 0, 0, 1700, 1100 };
	gboolean wheel = GPOINTER_TO_INT(data);
	gint i;
	fixture->tab.zoomedToFit = FALSE;
	gtk_widget_size_allocate(fixture->tab.ViewPort, &allocation);
	gtk_adjustment_configure(fixture->horizontal, 0, 0, 2024, 10, 100, 1698);
	gtk_adjustment_configure(fixture->vertical, 0, 0, 1824, 10, 100, 1098);
	/* The source point (300, 200) starts beneath this off-centre cursor. */
	for (i = 0; i < 30; i++) {
		scroll_at(fixture, wheel ? GDK_SCROLL_DOWN : GDK_SCROLL_SMOOTH,
				GDK_CONTROL_MASK, 0, 1, 812, 712);
		assert_anchor(fixture, 300, 200, 812, 712);
	}
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 0.05);
}

static void settle_zoom(NavigationFixture *fixture) {
	gint64 deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	do {
		while (gtk_events_pending())
			gtk_main_iteration();
		if (!fixture->tab.pendingZoomScrollOnAdjust
				&& gtk_widget_get_allocated_width(fixture->tab.drawing_area)
				== (gint) fixture->tab.viewCanvasSize[0]
				&& gtk_widget_get_allocated_height(fixture->tab.drawing_area)
				== (gint) fixture->tab.viewCanvasSize[1])
			return;
		g_usleep(1000);
	} while (g_get_monotonic_time() < deadline);
	g_error("Canvas did not finish resizing after zoom");
}

static void test_zoom_layout(NavigationFixture *fixture, gconstpointer data) {
	gint64 deadline;
	gint i;
	(void) data;
	fixture->tab.zoomedToFit = FALSE;
	gtk_window_resize(GTK_WINDOW(fixture->window), 1700, 1100);
	deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	while (gtk_widget_get_allocated_width(fixture->tab.ViewPort) < 1700
			&& g_get_monotonic_time() < deadline) {
		while (gtk_events_pending())
			gtk_main_iteration();
		g_usleep(1000);
	}
	g_assert_cmpint(gtk_widget_get_allocated_width(fixture->tab.ViewPort), >=, 1700);
	gtk_adjustment_set_value(fixture->horizontal, 0);
	gtk_adjustment_set_value(fixture->vertical, 0);
	for (i = 0; i < 30; i++) {
		/* Move the pointer after starting; the original anchor must stay put. */
		scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK,
				0, 1, i == 0 ? 812 : 1200, i == 0 ? 712 : 900);
		settle_zoom(fixture);
		assert_anchor(fixture, 300, 200, 812, 712);
	}
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 0.05);
	/* Reverse the same gesture and keep its anchor through canvas growth too. */
	for (i = 0; i < 20; i++) {
		scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -1, 1200, 900);
		settle_zoom(fixture);
		assert_anchor(fixture, 300, 200, 812, 712);
	}
}

static void test_edge_anchor(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	gtk_adjustment_set_value(fixture->horizontal, 400);
	gtk_adjustment_set_value(fixture->vertical, 400);
	/* Start in the grey margin: clamp to the visible top-left image corner. */
	scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -1, 20, 30);
	assert_anchor(fixture, 0, 0, 112, 112);
	scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -1, 200, 200);
	assert_anchor(fixture, 0, 0, 112, 112);
	/* A stop releases the anchor so the next gesture can choose another one. */
	scroll(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, 0);
	g_assert_false(fixture->tab.zoomAnchorActive);
	scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -1, 200, 200);
	g_assert_cmpfloat(fixture->tab.zoomAnchorImage[0], >, 0);
	g_assert_cmpfloat(fixture->tab.zoomAnchorImage[1], >, 0);
}

static void test_scroll_anchor_lifetime(NavigationFixture *fixture, gconstpointer data) {
	gdouble image_x, image_y;
	(void) data;
	scroll_at_time(fixture, GDK_SCROLL_UP, GDK_CONTROL_MASK, 0, 0, 100, 150, 1000);
	scroll_at_time(fixture, GDK_SCROLL_UP, GDK_CONTROL_MASK, 0, 0, 250, 220, 1150);
	assert_anchor(fixture, 188, 138, 100, 150);
	settle_zoom(fixture);
	image_x = (gtk_adjustment_get_value(fixture->horizontal) + 250
			- fixture->tab.viewOrigin[0]) / fixture->tab.viewZoom;
	image_y = (gtk_adjustment_get_value(fixture->vertical) + 220
			- fixture->tab.viewOrigin[1]) / fixture->tab.viewZoom;
	/* A pause starts a new wheel burst, now anchored at the new pointer. */
	scroll_at_time(fixture, GDK_SCROLL_UP, GDK_CONTROL_MASK, 0, 0, 250, 220, 1700);
	assert_anchor(fixture, image_x, image_y, 250, 220);
}

static void test_transformed_anchor(NavigationFixture *fixture, gconstpointer data) {
	const gdouble forward[9] = { 1, 0.2, 40, 0, 1, 30, 0, 0, 1 };
	const gdouble inverse[9] = { 1, -0.2, -34, 0, 1, -30, 0, 0, 1 };
	(void) data;
	fixture->tab.perspective.valid = fixture->tab.perspective.enabled = TRUE;
	memcpy(fixture->tab.perspectiveTransform.forward, forward, sizeof forward);
	memcpy(fixture->tab.perspectiveTransform.inverse, inverse, sizeof inverse);
	gtk_adjustment_set_value(fixture->horizontal, 400);
	gtk_adjustment_set_value(fixture->vertical, 400);
	/* This cursor maps outside the transformed image to source (-100, 100). */
	scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -1, 72, 242);
	/* Clamped source (0, 100) maps to displayed image pixel (60, 130). */
	assert_anchor(fixture, 60, 130, 172, 242);
	scroll_at(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, 1, 200, 100);
	assert_anchor(fixture, 60, 130, 172, 242);
}

static void test_two_axis_pan(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	scroll(fixture, GDK_SCROLL_SMOOTH, 0, 0.5, -0.25);
	g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(fixture->horizontal),
			615, 1e-9);
	g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(fixture->vertical),
			492.5, 1e-9);
	g_assert_false(fixture->tab.zoomedToFit);
	scroll(fixture, GDK_SCROLL_SMOOTH, 0, -0.5, 0.25);
	g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(fixture->horizontal),
			600, 1e-9);
	g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(fixture->vertical),
			500, 1e-9);
}

static void test_wheel(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	scroll(fixture, GDK_SCROLL_DOWN, GDK_SHIFT_MASK, 0, 0);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->horizontal), ==, 630);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->vertical), ==, 500);
	scroll(fixture, GDK_SCROLL_UP, 0, 0, 0);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->vertical), ==, 470);
	scroll(fixture, GDK_SCROLL_UP, GDK_CONTROL_MASK, 0, 0);
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 1.25);
	scroll(fixture, GDK_SCROLL_DOWN, GDK_CONTROL_MASK, 0, 0);
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 1);
}

static void test_smooth_zoom(NavigationFixture *fixture, gconstpointer data) {
	gdouble anchor_x, anchor_y;
	gint i;
	(void) data;
	anchor_x = 600 + 100 - fixture->tab.viewOrigin[0];
	anchor_y = 500 + 150 - fixture->tab.viewOrigin[1];
	/* Deliver a burst before GTK has a chance to resize the growing canvas. */
	for (i = 0; i < 10; i++)
		scroll(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, -0.1);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 1.25, 1e-9);
	g_assert_false(fixture->tab.zoomedToFit);
	g_assert_cmpfloat_with_epsilon(fixture->tab.pendingZoomScrollTarget[0],
			anchor_x * 1.25 + fixture->tab.viewOrigin[0] - 100, 1e-9);
	g_assert_cmpfloat_with_epsilon(fixture->tab.pendingZoomScrollTarget[1],
			anchor_y * 1.25 + fixture->tab.viewOrigin[1] - 150, 1e-9);
	for (i = 0; i < 10; i++)
		scroll(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, 0.1);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 1, 1e-9);
}

static void test_scroll_stop(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	scroll(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 0, 0);
	scroll(fixture, GDK_SCROLL_SMOOTH, GDK_CONTROL_MASK, 1, 0);
	scroll(fixture, GDK_SCROLL_SMOOTH, 0, 0, 0);
	g_assert_true(fixture->tab.zoomedToFit);
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 1);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->horizontal), ==, 600);
}

static void button(NavigationFixture *fixture, gboolean press, guint which) {
	GdkEventButton event = { 0 };
	gboolean handled = FALSE;
	event.type = press ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE;
	event.button = which;
	/* Release Ctrl before the button to verify that the pan remains latched. */
	event.state = press ? GDK_CONTROL_MASK : 0;
	event.x = 700;
	event.y = 650;
	event.x_root = event.y_root = 1000;
	g_signal_emit_by_name(fixture->tab.drawing_area,
			press ? "button-press-event" : "button-release-event", &event,
			&handled);
	g_assert_true(handled);
}

static void motion(NavigationFixture *fixture, gdouble dx, gdouble dy) {
	GdkEventMotion event = { 0 };
	gboolean handled = FALSE;
	event.type = GDK_MOTION_NOTIFY;
	event.x_root = 1000 + dx;
	event.y_root = 1000 + dy;
	g_signal_emit_by_name(fixture->tab.drawing_area, "motion-notify-event",
			&event, &handled);
	g_assert_true(handled);
}

static void test_ctrl_pan(NavigationFixture *fixture, gconstpointer data) {
	DataSeries *series = fixture->tab.document->active_series;
	SamplePoint *point = data_series_add_point(series, 188, 138);
	GdkCursor *pan_cursor;
	gint mode = GPOINTER_TO_INT(data);
	image_document_select_point(fixture->tab.document, series, point, FALSE);
	fixture->tab.setxypressed[0] = mode == 1;
	fixture->tab.perspectiveEditing = mode == 2;
	fixture->tab.perspectivePick = 0;
	button(fixture, TRUE, 1);
	g_assert_cmpuint(fixture->tab.panButton, ==, 1);
	pan_cursor = gdk_window_get_cursor(gtk_widget_get_window(fixture->tab.drawing_area));
	g_assert_nonnull(pan_cursor);
	g_object_ref(pan_cursor);
	motion(fixture, 0.5, 0.5);
	button(fixture, FALSE, 1);
	g_assert_true(fixture->tab.zoomedToFit);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->horizontal), ==, 600);
	button(fixture, TRUE, 1);
	motion(fixture, 40, -25);
	button(fixture, FALSE, 1);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->horizontal), ==, 560);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->vertical), ==, 525);
	g_assert_cmpuint(fixture->tab.panButton, ==, 0);
	g_assert_false(fixture->tab.zoomedToFit);
	g_assert_cmpuint(series->points->len, ==, 1);
	g_assert_cmpfloat(point->source_x_px, ==, 188);
	g_assert_cmpfloat(point->source_y_px, ==, 138);
	g_assert_null(fixture->tab.movedPoint);
	g_assert_false(fixture->tab.leftPressPending);
	g_assert_false(fixture->tab.perspectivePicked[0]);
	g_assert_cmpfloat(fixture->tab.axiscoords[0][0], ==, 0);
	g_assert_cmpint(fixture->tab.setxypressed[0], ==, mode == 1);
	g_assert_false(history_can_undo(fixture->tab.history));
	g_assert_nonnull(gdk_window_get_cursor(gtk_widget_get_window(fixture->tab.drawing_area)));
	g_assert_true(gdk_window_get_cursor(gtk_widget_get_window(
			fixture->tab.drawing_area)) != pan_cursor);
	g_object_unref(pan_cursor);
}

static void test_pan_cleanup(NavigationFixture *fixture, gconstpointer data) {
	GdkEventGrabBroken event = { 0 };
	gboolean handled;
	(void) data;
	button(fixture, TRUE, 2);
	motion(fixture, 40, -25);
	button(fixture, FALSE, 1);
	g_assert_cmpuint(fixture->tab.panButton, ==, 2);
	button(fixture, FALSE, 2);
	g_assert_cmpuint(fixture->tab.panButton, ==, 0);
	g_assert_cmpfloat(gtk_adjustment_get_value(fixture->horizontal), ==, 560);
	button(fixture, TRUE, 1);
	event.type = GDK_GRAB_BROKEN;
	g_signal_emit_by_name(fixture->tab.drawing_area, "grab-broken-event",
			&event, &handled);
	g_assert_cmpuint(fixture->tab.panButton, ==, 0);
	button(fixture, TRUE, 1);
	gtk_widget_hide(fixture->window);
	g_assert_cmpuint(fixture->tab.panButton, ==, 0);
}

#if GTK_CHECK_VERSION(3, 18, 0)
static void pinch(NavigationFixture *fixture, GdkTouchpadGesturePhase phase,
		gdouble scale) {
	GdkEvent event = { 0 };
	gboolean handled = FALSE;
	event.touchpad_pinch.type = GDK_TOUCHPAD_PINCH;
	event.touchpad_pinch.n_fingers = 2;
	event.touchpad_pinch.phase = phase;
	event.touchpad_pinch.scale = scale;
	event.touchpad_pinch.x = gtk_adjustment_get_value(fixture->horizontal) + 100;
	event.touchpad_pinch.y = gtk_adjustment_get_value(fixture->vertical) + 150;
	g_signal_emit_by_name(fixture->tab.drawing_area, "event", &event, &handled);
	g_assert_true(handled);
}

static void test_pinch(NavigationFixture *fixture, gconstpointer data) {
	(void) data;
	g_assert_true((gtk_widget_get_events(fixture->tab.drawing_area)
			& GDK_TOUCHPAD_GESTURE_MASK) != 0);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_BEGIN, 1);
	g_assert_true(fixture->tab.zoomedToFit);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 1.1);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 1.2);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 1.2, 1e-9);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_END, 1.2);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 2);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 1.2, 1e-9);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_BEGIN, 1);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 0.5);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 0.6, 1e-9);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_CANCEL, 0.5);
	g_assert_false(fixture->tab.pinchActive);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_BEGIN, 1);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, NAN);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 0);
	g_assert_cmpfloat_with_epsilon(fixture->tab.viewZoom, 0.6, 1e-9);
	pinch(fixture, GDK_TOUCHPAD_GESTURE_PHASE_UPDATE, 1000);
	g_assert_cmpfloat(fixture->tab.viewZoom, ==, 16);
}
#endif

int main(int argc, char **argv) {
	gtk_test_init(&argc, &argv, NULL);
#define ADD_TEST(path, data, test) \
	g_test_add(path, NavigationFixture, data, setup, test, teardown)
	ADD_TEST("/navigation/two-axis-pan", NULL, test_two_axis_pan);
	ADD_TEST("/navigation/mouse-wheel", NULL, test_wheel);
	ADD_TEST("/navigation/smooth-zoom-anchor", NULL, test_smooth_zoom);
	ADD_TEST("/navigation/extreme-smooth-zoom", NULL, test_extreme_zoom);
	ADD_TEST("/navigation/extreme-wheel-zoom", GINT_TO_POINTER(1), test_extreme_zoom);
	ADD_TEST("/navigation/zoom-layout", NULL, test_zoom_layout);
	ADD_TEST("/navigation/edge-anchor", NULL, test_edge_anchor);
	ADD_TEST("/navigation/scroll-anchor-lifetime", NULL, test_scroll_anchor_lifetime);
	ADD_TEST("/navigation/transformed-anchor", NULL, test_transformed_anchor);
	ADD_TEST("/navigation/scroll-stop", NULL, test_scroll_stop);
	ADD_TEST("/navigation/ctrl-pan-sampling", NULL, test_ctrl_pan);
	ADD_TEST("/navigation/ctrl-pan-calibration", GINT_TO_POINTER(1), test_ctrl_pan);
	ADD_TEST("/navigation/ctrl-pan-perspective", GINT_TO_POINTER(2), test_ctrl_pan);
	ADD_TEST("/navigation/pan-cleanup", NULL, test_pan_cleanup);
#if GTK_CHECK_VERSION(3, 18, 0)
	ADD_TEST("/navigation/pinch", NULL, test_pinch);
#endif
#undef ADD_TEST
	return g_test_run();
}
