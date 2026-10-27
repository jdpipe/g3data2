/* Perspective geometry, rendering, and storage regression tests. GPL-2.0-or-later. */
#include <CUnit/Basic.h>
#include <glib/gstdio.h>
#include <math.h>
#include <string.h>
#include "../main.h"
#include "../datastore.h"

static PerspectiveState example(void) {
    PerspectiveState state = { TRUE, TRUE, {{20,20},{180,30},{160,170},{40,180}} };
    return state;
}

void test_perspective_geometry(void) {
    PerspectiveState state = example(), bad;
    PerspectiveTransform t;
    struct TabData tab = {0};
    struct PointValue value;
    gdouble mapped[4][2], sx,sy,x,y,gx[2],gy[2], axes[4][2];
    GError *error = NULL;
    gint i;
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,201,&t,&error));
    CU_ASSERT_PTR_NULL(error);
    for (i=0;i<4;i++) {
        CU_ASSERT_TRUE(perspective_map(t.forward,state.corners[i][0],state.corners[i][1],&mapped[i][0],&mapped[i][1]));
        CU_ASSERT_TRUE(perspective_map(t.inverse,mapped[i][0],mapped[i][1],&x,&y));
        CU_ASSERT_DOUBLE_EQUAL(x,state.corners[i][0],1e-9);
        CU_ASSERT_DOUBLE_EQUAL(y,state.corners[i][1],1e-9);
    }
    CU_ASSERT_DOUBLE_EQUAL(mapped[0][1],mapped[1][1],1e-9);
    CU_ASSERT_DOUBLE_EQUAL(mapped[1][0],mapped[2][0],1e-9);
    CU_ASSERT_DOUBLE_EQUAL(mapped[2][1],mapped[3][1],1e-9);
    CU_ASSERT_DOUBLE_EQUAL(mapped[3][0],mapped[0][0],1e-9);
    tab.perspective = state; tab.perspectiveTransform = t;
    memcpy(tab.axiscoords[0],state.corners[3],sizeof(tab.axiscoords[0]));
    memcpy(tab.axiscoords[1],state.corners[2],sizeof(tab.axiscoords[1]));
    memcpy(tab.axiscoords[2],state.corners[3],sizeof(tab.axiscoords[2]));
    memcpy(tab.axiscoords[3],state.corners[0],sizeof(tab.axiscoords[3]));
    memcpy(axes,tab.axiscoords,sizeof(axes));
    tab.realcoords[0]=0; tab.realcoords[1]=10;
    tab.realcoords[2]=0; tab.realcoords[3]=20;
    x=mapped[3][0]+0.3*(mapped[2][0]-mapped[3][0]);
    y=mapped[3][1]+0.6*(mapped[0][1]-mapped[3][1]);
    CU_ASSERT_TRUE(perspective_map(t.inverse,x,y,&sx,&sy));
    value = calculatePointValue(sx,sy,&tab);
    CU_ASSERT_DOUBLE_EQUAL(value.Xv,3,1e-9);
    CU_ASSERT_DOUBLE_EQUAL(value.Yv,12,1e-9);
    CU_ASSERT_TRUE(isfinite(value.Xerr) && isfinite(value.Yerr));
    CU_ASSERT_TRUE(calculateAxisGuides(sx,sy,&tab,gx,gy));
    CU_ASSERT_TRUE(perspective_map(t.forward,gx[0],gx[1],&gx[0],&gx[1]));
    CU_ASSERT_TRUE(perspective_map(t.forward,gy[0],gy[1],&gy[0],&gy[1]));
    CU_ASSERT_DOUBLE_EQUAL(gx[0],x,1e-9);
    CU_ASSERT_DOUBLE_EQUAL(gx[1],mapped[3][1],1e-9);
    CU_ASSERT_DOUBLE_EQUAL(gy[0],mapped[3][0],1e-9);
    CU_ASSERT_DOUBLE_EQUAL(gy[1],y,1e-9);
    tab.perspective.enabled=FALSE;
    value=calculatePointValue(sx,sy,&tab);
    CU_ASSERT_TRUE(fabs(value.Xv-3)>1e-4 || fabs(value.Yv-12)>1e-4);
    tab.perspective.enabled=TRUE;
    tab.logxy[0]=TRUE; tab.realcoords[0]=1; tab.realcoords[1]=100;
    value=calculatePointValue(sx,sy,&tab);
    CU_ASSERT_DOUBLE_EQUAL(value.Xv,pow(100,0.3),1e-9);
    CU_ASSERT_EQUAL(memcmp(axes,tab.axiscoords,sizeof(axes)),0);
    /* The display cache is bounded even for a large source. Changing that
     * display resolution must not change calibrated values or source units. */
    bad=state;
    for (i=0;i<4;i++) {
        bad.corners[i][0] *= 1000;
        bad.corners[i][1] *= 1000;
        tab.axiscoords[i][0] *= 1000;
        tab.axiscoords[i][1] *= 1000;
    }
    CU_ASSERT_TRUE_FATAL(perspective_build(&bad,201000,201000,&t,&error));
    CU_ASSERT_TRUE(t.width<=16000 && t.height<=16000);
    CU_ASSERT_TRUE((gint64)t.width*t.height<=32040000);
    tab.perspective=bad; tab.perspectiveTransform=t;
    value=calculatePointValue(sx*1000,sy*1000,&tab);
    CU_ASSERT_DOUBLE_EQUAL(value.Xv,pow(100,0.3),1e-9);
    CU_ASSERT_DOUBLE_EQUAL(value.Yv,12,1e-9);
    bad=state; memcpy(bad.corners[1],bad.corners[0],sizeof(bad.corners[0]));
    CU_ASSERT_FALSE(perspective_build(&bad,201,201,&t,&error)); g_clear_error(&error);
    bad=state; memcpy(bad.corners[1],state.corners[2],sizeof(bad.corners[1]));
    memcpy(bad.corners[2],state.corners[1],sizeof(bad.corners[2]));
    CU_ASSERT_FALSE(perspective_build(&bad,201,201,&t,&error)); g_clear_error(&error);
    bad=(PerspectiveState){TRUE,TRUE,{{40,40},{60,40},{90,60},{10,60}}};
    CU_ASSERT_FALSE(perspective_build(&bad,101,101,&t,&error)); g_clear_error(&error);
    bad=state; bad.corners[0][0]=NAN;
    CU_ASSERT_FALSE(perspective_build(&bad,201,201,&t,&error)); g_clear_error(&error);
}

void test_perspective_render(void) {
    PerspectiveState state = example();
    PerspectiveTransform t;
    cairo_surface_t *source,*result;
    cairo_t *cr;
    gint x,y,clear=0,painted=0,stride;
    guchar *pixels;
    GError *error=NULL;
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,201,&t,&error));
    source=cairo_image_surface_create(CAIRO_FORMAT_RGB24,201,201);
    cr=cairo_create(source); cairo_set_source_rgb(cr,1,0,0); cairo_paint(cr); cairo_destroy(cr);
    result=perspective_render(source,1,&t,&error);
    CU_ASSERT_PTR_NULL(error); CU_ASSERT_PTR_NOT_NULL_FATAL(result);
    CU_ASSERT_EQUAL(cairo_image_surface_get_width(result),t.width);
    CU_ASSERT_EQUAL(cairo_image_surface_get_height(result),t.height);
    cairo_surface_flush(result);
    pixels=cairo_image_surface_get_data(result); stride=cairo_image_surface_get_stride(result);
    for (y=0;y<t.height;y++) for (x=0;x<t.width;x++) {
        guint32 pixel=((guint32 *)(pixels+(gsize)y*stride))[x];
        if (pixel==0) clear++;
        else if (pixel==0xffff0000u) painted++;
        else CU_FAIL("Unexpected colour or alpha in warped bitmap");
    }
    CU_ASSERT_TRUE(clear>0); CU_ASSERT_TRUE(painted>0);
    cairo_surface_destroy(result);
    state=(PerspectiveState){TRUE,TRUE,{{0,0},{200,0},{200,200},{0,200}}};
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,201,&t,&error));
    CU_ASSERT_EQUAL(t.width,201); CU_ASSERT_EQUAL(t.height,201);
    result=perspective_render(source,1,&t,&error);
    CU_ASSERT_PTR_NOT_NULL_FATAL(result);
    cairo_surface_flush(result);
    CU_ASSERT_EQUAL(*(guint32 *)cairo_image_surface_get_data(result),0xffff0000u);
    cairo_surface_destroy(result); cairo_surface_destroy(source);
}

void test_perspective_storage(void) {
    gchar *directory=g_dir_make_tmp("g3data2-perspective-XXXXXX",NULL);
    gchar *dbpath=g_build_filename(directory,"test.sqlite3",NULL);
    gchar *path=g_build_filename(directory,"source",NULL);
    Datastore *store;
    PerspectiveState state=example(),loaded;
    CalibrationState calibration,restored;
    ImageDocument *doc,*copy;
    DataSeries *series;
    SamplePoint *point;
    gint64 image_id;
    gboolean existing;
    GError *error=NULL;
    g_file_set_contents(path,"image",-1,NULL);
    store=datastore_open(dbpath,&error); CU_ASSERT_PTR_NOT_NULL_FATAL(store);
    CU_ASSERT_TRUE(datastore_resolve_image(store,path,201,201,&image_id,&existing,&error));
    CU_ASSERT_TRUE(datastore_load_perspective(store,image_id,&loaded,&error));
    CU_ASSERT_FALSE(loaded.valid); CU_ASSERT_FALSE(loaded.enabled);
    doc=image_document_new(image_id);
    series=image_document_add_series(doc,"A",G3_COLOR_RED);
    CU_ASSERT_TRUE(datastore_insert_series(store,image_id,series,&error));
    point=data_series_add_point(series,83.125,102.75);
    CU_ASSERT_TRUE(datastore_insert_point(store,series->id,point,&error));
    calibration_state_clear(&calibration);
    calibration.axis_x[0]=40; calibration.axis_y[0]=180; calibration.position_set[0]=TRUE;
    CU_ASSERT_TRUE(datastore_save_calibration(store,image_id,&calibration,&error));
    CU_ASSERT_TRUE(datastore_save_perspective(store,image_id,&state,&error));
    datastore_close(store);
    store=datastore_open(dbpath,&error); CU_ASSERT_PTR_NOT_NULL_FATAL(store);
    CU_ASSERT_TRUE(datastore_load_perspective(store,image_id,&loaded,&error));
    CU_ASSERT_TRUE(loaded.enabled && loaded.valid);
    CU_ASSERT_EQUAL(memcmp(loaded.corners,state.corners,sizeof(state.corners)),0);
    state.enabled=FALSE;
    CU_ASSERT_TRUE(datastore_save_perspective(store,image_id,&state,&error));
    CU_ASSERT_TRUE(datastore_load_perspective(store,image_id,&loaded,&error));
    CU_ASSERT_FALSE(loaded.enabled); CU_ASSERT_TRUE(loaded.valid);
    CU_ASSERT_EQUAL(memcmp(loaded.corners,state.corners,sizeof(state.corners)),0);
    copy=image_document_new(image_id);
    CU_ASSERT_TRUE(datastore_load_document(store,image_id,copy,&restored,&error));
    point=g_ptr_array_index(((DataSeries *)g_ptr_array_index(copy->series,0))->points,0);
    CU_ASSERT_DOUBLE_EQUAL(point->source_x_px,83.125,0);
    CU_ASSERT_DOUBLE_EQUAL(point->source_y_px,102.75,0);
    CU_ASSERT_DOUBLE_EQUAL(restored.axis_x[0],40,0);
    CU_ASSERT_DOUBLE_EQUAL(restored.axis_y[0],180,0);
    CU_ASSERT_PTR_NULL(error);
    image_document_free(doc); image_document_free(copy); datastore_close(store);
    g_unlink(path); g_unlink(dbpath); g_rmdir(directory);
    g_free(path); g_free(dbpath); g_free(directory);
}

static gdouble pixel_scale_from_square(const PerspectiveTransform *transform,
        gdouble x, gdouble y) {
    const gdouble epsilon = 0.01;
    gdouble offsets[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    gdouble p[4][2], area;
    gint i;
    for (i=0;i<4;i++) {
        CU_ASSERT_TRUE(perspective_map(transform->forward,
            x+epsilon*offsets[i][0],y+epsilon*offsets[i][1],&p[i][0],&p[i][1]));
    }
    area = fabs((p[2][0]-p[0][0])*(p[3][1]-p[1][1])
              - (p[2][1]-p[0][1])*(p[3][0]-p[1][0])) / 2;
    return sqrt(area/(4*epsilon*epsilon));
}

void test_perspective_pixel_scale(void) {
    PerspectiveState state = {TRUE,TRUE,{{0,0},{200,0},{200,200},{0,200}}};
    PerspectiveTransform transform;
    gdouble left,top,right,bottom,x,y,expected,centroid_x=0,centroid_y=0;
    gint i;
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,201,&transform,NULL));
    CU_ASSERT_DOUBLE_EQUAL(transform.center_pixel_scale,1,1e-12);
    /* An affine shear stretches different directions differently. The circle
     * uses the area-equivalent size, not either axis's individual length. */
    state=(PerspectiveState){TRUE,TRUE,{{20,20},{140,20},{180,100},{60,100}}};
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,121,&transform,NULL));
    CU_ASSERT_DOUBLE_EQUAL(transform.center_pixel_scale,pow(1.25,0.25),1e-12);
    state=example();
    CU_ASSERT_TRUE_FATAL(perspective_build(&state,201,201,&transform,NULL));
    CU_ASSERT_TRUE(perspective_map(transform.forward,state.corners[0][0],state.corners[0][1],&left,&top));
    CU_ASSERT_TRUE(perspective_map(transform.forward,state.corners[2][0],state.corners[2][1],&right,&bottom));
    CU_ASSERT_TRUE(perspective_map(transform.inverse,(left+right)/2,(top+bottom)/2,&x,&y));
    expected=pixel_scale_from_square(&transform,x,y);
    CU_ASSERT_DOUBLE_EQUAL(transform.center_pixel_scale,expected,1e-7);
    for (i=0;i<4;i++) {
        centroid_x+=state.corners[i][0]/4;
        centroid_y+=state.corners[i][1]/4;
    }
    CU_ASSERT_TRUE(fabs(expected-pixel_scale_from_square(&transform,centroid_x,centroid_y))>1e-4);
    /* Uniformly changing the rendered cache resolution scales the reference
     * pixel size by the same factor, before the view zoom is applied. */
    for (i=0;i<6;i++) transform.forward[i]*=0.25;
    CU_ASSERT_DOUBLE_EQUAL(pixel_scale_from_square(&transform,x,y),expected*0.25,1e-7);
}
