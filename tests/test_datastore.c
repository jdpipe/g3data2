#include <CUnit/Basic.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <math.h>
#include <sqlite3.h>
#include <string.h>

#include "../datastore.h"
#include "../main.h"

static gchar *test_directory;
static gchar *database_path;
static gchar *image_path;
static Datastore *datastore;

static int suite_setup(void) {
	GError *error;

	error = NULL;
	test_directory = g_dir_make_tmp("g3data2-test-XXXXXX", &error);
	if (test_directory == NULL) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		return 1;
	}
	database_path = g_build_filename(test_directory, "test.sqlite3", NULL);
	image_path = g_build_filename(test_directory, "plot.png", NULL);
	if (!g_file_set_contents(image_path, "not really a png", -1, &error)) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		return 1;
	}
	datastore = datastore_open(database_path, &error);
	if (datastore == NULL) {
		g_printerr("%s\n", error->message);
		g_clear_error(&error);
		return 1;
	}
	return 0;
}

static int suite_teardown(void) {
	gchar *wal, *shm;

	datastore_close(datastore);
	wal = g_strconcat(database_path, "-wal", NULL);
	shm = g_strconcat(database_path, "-shm", NULL);
	g_remove(image_path);
	g_remove(database_path);
	g_remove(wal);
	g_remove(shm);
	g_rmdir(test_directory);
	g_free(wal);
	g_free(shm);
	g_free(image_path);
	g_free(database_path);
	g_free(test_directory);
	return 0;
}

static gint64 resolve_image(const gchar *path, gint width, gint height,
		gboolean *existing) {
	GError *error;
	gint64 image_id;
	gboolean result;

	error = NULL;
	image_id = 0;
	result = datastore_resolve_image(datastore, path, width, height, &image_id,
			existing, &error);
	CU_ASSERT_TRUE_FATAL(result);
	CU_ASSERT_PTR_NULL_FATAL(error);
	return image_id;
}

static void test_image_identity(void) {
	gchar *moved_path;
	gboolean existing;
	gint64 first_id, moved_id, replaced_id;
	GError *error;

	first_id = resolve_image(image_path, 640, 480, &existing);
	CU_ASSERT_FALSE(existing);
	first_id = resolve_image(image_path, 640, 480, &existing);
	CU_ASSERT_TRUE(existing);

	moved_path = g_build_filename(test_directory, "renamed.png", NULL);
	error = NULL;
	CU_ASSERT_TRUE_FATAL(g_file_set_contents(moved_path, "not really a png", -1,
			&error));
	CU_ASSERT_PTR_NULL_FATAL(error);
	moved_id = resolve_image(moved_path, 640, 480, &existing);
	CU_ASSERT_TRUE(existing);
	CU_ASSERT_EQUAL(first_id, moved_id);
	error = NULL;
	CU_ASSERT_FALSE(datastore_resolve_image(datastore, moved_path, 320, 200,
			&moved_id, &existing, &error));
	CU_ASSERT_PTR_NOT_NULL(error);
	CU_ASSERT_EQUAL(DATASTORE_ERROR_IDENTITY, error->code);
	g_clear_error(&error);

	CU_ASSERT_TRUE_FATAL(g_file_set_contents(moved_path, "different bytes", -1,
			&error));
	replaced_id = resolve_image(moved_path, 320, 200, &existing);
	CU_ASSERT_FALSE(existing);
	CU_ASSERT_NOT_EQUAL(first_id, replaced_id);
	g_remove(moved_path);
	g_free(moved_path);
}

static void test_round_trip(void) {
	CalibrationState saved, loaded;
	ImageDocument *written, *restored;
	DataSeries *series, *restored_series;
	SamplePoint *point, *restored_point;
	gboolean existing;
	gint64 image_id;
	GError *error;

	error = NULL;
	image_id = resolve_image(image_path, 640, 480, &existing);
	calibration_state_clear(&saved);
	saved.log_axis[0] = TRUE;
	saved.position_set[0] = saved.position_set[1] = TRUE;
	saved.value_set[0] = saved.value_set[1] = TRUE;
	saved.axis_x[0] = 10.25;
	saved.axis_y[0] = 470.75;
	saved.axis_value[0] = 1.0;
	saved.axis_x[1] = 630.125;
	saved.axis_y[1] = 469.5;
	saved.axis_value[1] = 1000.0;
	CU_ASSERT_TRUE_FATAL(datastore_save_calibration(datastore, image_id, &saved,
			&error));
	CU_ASSERT_PTR_NULL_FATAL(error);

	written = image_document_new(image_id);
	series = image_document_add_series(written, "Upper curve", 0x1f77b4ffu);
	CU_ASSERT_TRUE_FATAL(datastore_insert_series(datastore, image_id, series,
			&error));
	point = data_series_add_point(series, 123.456789, 234.567891);
	CU_ASSERT_TRUE_FATAL(datastore_insert_point(datastore, series->id, point,
			&error));
	CU_ASSERT_TRUE_FATAL(datastore_set_active_series(datastore, image_id,
			series->id, &error));

	restored = image_document_new(image_id);
	CU_ASSERT_TRUE_FATAL(datastore_load_document(datastore, image_id, restored,
			&loaded, &error));
	CU_ASSERT_PTR_NULL_FATAL(error);
	CU_ASSERT_TRUE(loaded.log_axis[0]);
	CU_ASSERT_TRUE(loaded.position_set[0]);
	CU_ASSERT_TRUE(loaded.value_set[1]);
	CU_ASSERT_DOUBLE_EQUAL(10.25, loaded.axis_x[0], 1e-12);
	CU_ASSERT_DOUBLE_EQUAL(1000.0, loaded.axis_value[1], 1e-12);
	CU_ASSERT_EQUAL(1, restored->series->len);
	restored_series = g_ptr_array_index(restored->series, 0);
	CU_ASSERT_STRING_EQUAL("Upper curve", restored_series->label);
	CU_ASSERT_EQUAL(0x1f77b4ffu, restored_series->marker_rgba);
	CU_ASSERT_PTR_EQUAL(restored_series, restored->active_series);
	CU_ASSERT_EQUAL(1, restored_series->points->len);
	restored_point = g_ptr_array_index(restored_series->points, 0);
	CU_ASSERT_DOUBLE_EQUAL(123.456789, restored_point->source_x_px, 1e-12);
	CU_ASSERT_DOUBLE_EQUAL(234.567891, restored_point->source_y_px, 1e-12);

	restored_point->source_x_px = 321.125;
	CU_ASSERT_TRUE(datastore_update_point(datastore, restored_point, &error));
	CU_ASSERT_TRUE(datastore_load_document(datastore, image_id, restored, &loaded,
			&error));
	CU_ASSERT_EQUAL(1, restored->series->len);
	restored_series = g_ptr_array_index(restored->series, 0);
	restored_point = g_ptr_array_index(restored_series->points, 0);
	CU_ASSERT_DOUBLE_EQUAL(321.125, restored_point->source_x_px, 1e-12);
	g_free(restored_series->label);
	restored_series->label = g_strdup("Renamed curve");
	restored_series->marker_rgba = 0xabcdef12u;
	restored_series->visible = FALSE;
	CU_ASSERT_TRUE(datastore_update_series(datastore, restored_series, &error));
	CU_ASSERT_TRUE(datastore_delete_series_points(datastore, restored_series->id,
			&error));

	/* Loading into the same model replaces, rather than duplicates, its data. */
	CU_ASSERT_TRUE(datastore_load_document(datastore, image_id, restored, &loaded,
			&error));
	CU_ASSERT_EQUAL(1, restored->series->len);
	restored_series = g_ptr_array_index(restored->series, 0);
	CU_ASSERT_STRING_EQUAL("Renamed curve", restored_series->label);
	CU_ASSERT_EQUAL(0xabcdef12u, restored_series->marker_rgba);
	CU_ASSERT_FALSE(restored_series->visible);
	CU_ASSERT_EQUAL(0, restored_series->points->len);
	point = data_series_add_point(restored_series, 400.5, 300.25);
	CU_ASSERT_TRUE(datastore_insert_point(datastore, restored_series->id, point,
			&error));
	CU_ASSERT_TRUE(datastore_delete_series(datastore, restored_series->id, &error));
	image_document_free(restored);
	image_document_free(written);

	restored = image_document_new(image_id);
	CU_ASSERT_TRUE(datastore_load_document(datastore, image_id, restored, &loaded,
			&error));
	CU_ASSERT_EQUAL(0, restored->series->len);
	{
		sqlite3 *check_database;
		sqlite3_stmt *statement;
		check_database = NULL;
		statement = NULL;
		CU_ASSERT_EQUAL(SQLITE_OK, sqlite3_open(database_path, &check_database));
		CU_ASSERT_EQUAL(SQLITE_OK, sqlite3_prepare_v2(check_database,
				"SELECT count(*) FROM points;", -1, &statement, NULL));
		CU_ASSERT_EQUAL(SQLITE_ROW, sqlite3_step(statement));
		CU_ASSERT_EQUAL(0, sqlite3_column_int(statement, 0));
		sqlite3_finalize(statement);
		sqlite3_close(check_database);
	}
	image_document_free(restored);
}

static void test_model_helpers(void) {
	ImageDocument *document;
	DataSeries *first, *second;
	SamplePoint *point, *point2;
	gchar *label;
	guint32 color;

	document = image_document_new(1);
	label = image_document_next_series_label(document);
	CU_ASSERT_STRING_EQUAL("Series 1", label);
	first = image_document_add_series(document, label,
			image_document_next_color(document));
	g_free(label);
	second = image_document_add_series(document, "Series 2",
			image_document_next_color(document));
	CU_ASSERT_PTR_EQUAL(first, document->active_series);
	image_document_set_active_series(document, second);
	CU_ASSERT_PTR_EQUAL(second, document->active_series);
	point = data_series_add_point(second, 10.5, 20.25);
	CU_ASSERT_EQUAL(0, point->sample_order);
	CU_ASSERT_EQUAL(0, data_series_index_of_point(second, point));
	point2 = data_series_add_point(second, 11.5, 21.25);
	image_document_select_point(document, second, point, FALSE);
	CU_ASSERT_EQUAL(1, image_document_selection_count(document));
	CU_ASSERT_TRUE(image_document_point_is_selected(document, point));
	image_document_select_point(document, second, point2, TRUE);
	CU_ASSERT_EQUAL(2, image_document_selection_count(document));
	image_document_select_point(document, second, point, TRUE);
	CU_ASSERT_EQUAL(1, image_document_selection_count(document));
	CU_ASSERT_PTR_EQUAL(point2, document->selected_point);
	CU_ASSERT_TRUE(rgba_from_string("#12345678", &color));
	CU_ASSERT_EQUAL(0x12345678u, color);
	CU_ASSERT_TRUE(image_document_remove_series(document, second));
	CU_ASSERT_PTR_EQUAL(first, document->active_series);
	CU_ASSERT_EQUAL(0, image_document_selection_count(document));
	image_document_free(document);
}

static void test_rejects_newer_schema(void) {
	gchar *path;
	sqlite3 *database;
	Datastore *newer;
	GError *error;

	path = g_build_filename(test_directory, "newer.sqlite3", NULL);
	database = NULL;
	CU_ASSERT_EQUAL(SQLITE_OK, sqlite3_open(path, &database));
	CU_ASSERT_EQUAL(SQLITE_OK,
			sqlite3_exec(database, "PRAGMA user_version=999;", NULL, NULL, NULL));
	sqlite3_close(database);
	error = NULL;
	newer = datastore_open(path, &error);
	CU_ASSERT_PTR_NULL(newer);
	CU_ASSERT_PTR_NOT_NULL_FATAL(error);
	CU_ASSERT_EQUAL(DATASTORE_ERROR_OPEN, error->code);
	g_clear_error(&error);
	g_remove(path);
	g_free(path);
}

static void test_active_series_export_recalculates(void) {
	struct TabData tab;
	ImageDocument *document;
	DataSeries *first, *second;
	GString *output;

	memset(&tab, 0, sizeof(tab));
	document = image_document_new(1);
	first = image_document_add_series(document, "First", G3_COLOR_RED);
	second = image_document_add_series(document, "Second", 0x1f77b4ffu);
	data_series_add_point(first, 25.0, 75.0);
	data_series_add_point(second, 50.0, 50.0);
	image_document_set_active_series(document, second);
	tab.document = document;
	tab.axiscoords[0][0] = 0.0;
	tab.axiscoords[0][1] = 100.0;
	tab.axiscoords[1][0] = 100.0;
	tab.axiscoords[1][1] = 100.0;
	tab.axiscoords[2][0] = 0.0;
	tab.axiscoords[2][1] = 100.0;
	tab.axiscoords[3][0] = 0.0;
	tab.axiscoords[3][1] = 0.0;
	tab.realcoords[0] = 0.0;
	tab.realcoords[1] = 10.0;
	tab.realcoords[2] = 0.0;
	tab.realcoords[3] = 20.0;

	output = formatResultset(&tab, FALSE, FALSE);
	CU_ASSERT_PTR_NOT_NULL_FATAL(output);
	CU_ASSERT_STRING_EQUAL("5  10\n", output->str);
	g_string_free(output, TRUE);

	output = formatResultset(&tab, TRUE, FALSE);
	CU_ASSERT_PTR_NOT_NULL_FATAL(output);
	CU_ASSERT_STRING_EQUAL("# First\n2.5  5\n# Second\n5  10\n", output->str);
	g_string_free(output, TRUE);

	output = formatResultset(&tab, TRUE, TRUE);
	CU_ASSERT_PTR_NOT_NULL_FATAL(output);
	CU_ASSERT_STRING_EQUAL("# First\n2.5\t5\n# Second\n5\t10\n", output->str);
	g_string_free(output, TRUE);

	/* The raw source point is unchanged; export uses the new calibration. */
	tab.realcoords[1] = 20.0;
	output = formatResultset(&tab, FALSE, FALSE);
	CU_ASSERT_PTR_NOT_NULL_FATAL(output);
	CU_ASSERT_STRING_EQUAL("10  10\n", output->str);
	g_string_free(output, TRUE);

	/* File-menu export preferences feed the same ordering/error fields. */
	data_series_add_point(second, 20.0, 25.0);
	tab.ordering = 1;
	tab.UseErrors = TRUE;
	output = formatResultset(&tab, FALSE, TRUE);
	CU_ASSERT_PTR_NOT_NULL_FATAL(output);
	CU_ASSERT_STRING_EQUAL("4\t15\t0.1\t0.1\n10\t10\t0.1\t0.1\n", output->str);
	g_string_free(output, TRUE);
	image_document_free(document);
}

int main(void) {
	CU_pSuite suite;
	unsigned int failures;

	if (CU_initialize_registry() != CUE_SUCCESS)
		return CU_get_error();
	suite = CU_add_suite("datastore", suite_setup, suite_teardown);
	if (suite == NULL
			|| CU_add_test(suite, "image identity", test_image_identity) == NULL
			|| CU_add_test(suite, "persistence round trip", test_round_trip) == NULL
			|| CU_add_test(suite, "model helpers", test_model_helpers) == NULL
			|| CU_add_test(suite, "reject newer schema",
					test_rejects_newer_schema) == NULL
			|| CU_add_test(suite, "active series export recalculates",
					test_active_series_export_recalculates) == NULL) {
		CU_cleanup_registry();
		return CU_get_error();
	}
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	failures = CU_get_number_of_failures();
	CU_cleanup_registry();
	return failures == 0 ? 0 : 1;
}
