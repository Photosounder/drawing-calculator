#ifndef DRAWCALC_AS_A_LIBRARY
#include "rl.h"
#endif

#define DRAWCALC_ELEM_LIMIT (256<<10)

typedef enum
{
	type_line,
	type_rect,
	type_quad,
	type_circle,
	type_number,
	type_text,
} symb_type_t;

struct line
{
	frgb_t col;
	xy_t p0, p1;
	double blur;
};

struct rect
{
	frgb_t col;
	rect_t rect;
};

struct quad
{
	frgb_t col;
	xy_t p[4];
	double blur;
};

struct circle
{
	frgb_t col;
	xy_t pos;
	double radius;
};

struct number
{
	frgb_t col;
	xy_t pos;
	double scale, value;
	int8_t prec, alig;
};

#define TEXT_VAL_COUNT 2
struct text
{
	frgb_t col;
	xy_t pos;
	double scale;
	uint64_t v[TEXT_VAL_COUNT];
	int8_t alig;
};

typedef struct
{
	symb_type_t type;
	union
	{
		struct line line;
		struct rect rect;
		struct quad quad;
		struct circle circle;
		struct number number;
		struct text text;
	} symb;
} drawcalc_symbol_t;

typedef struct
{
	volatile int thread_on;
	volatile int32_t inputs_changed;
	rl_thread_t thread_handle;
	char *expr_string;
	double angle_v, k[5];
	double angle_next;
	double time_v, time_next, time_rate_v;
	int animation;

	drawcalc_symbol_t *symbol0, *symbol1;
	size_t symbol0_count, symbol0_as, symbol1_count, symbol1_as;
	rl_mutex_t array_mutex;

	frgb_t colour_cur;

	// Used only by main thread:
	int recalc;
} drawcalc_t;

drawcalc_t drawcalc={0};

#define OPACITY 0.98

void drawcalc_compilation_log(char *comp_log)
{
	static int init=1;

	static flwindow_t window={0};
	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"elem 0", "type none", "label Compilation log", "pos	0	0", "dim	9;4	2", "off	0	1", "",
		"elem 10", "type textedit", "pos	0;3	-0;9", "dim	8;10	1", "off	0	1", "",
	};

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Drawing calc compilation log");

	if (mouse.window_minimised_flag > 0)
		return;

	if (init)
	{
		init = 0;

		get_textedit_fromlayout(&layout, 10)->read_only = 1;
		get_textedit_fromlayout(&layout, 10)->first_click_no_sel = 1;
		get_textedit_fromlayout(&layout, 10)->edit_mode = te_mode_full;
		print_to_layout_textedit(&layout, 10, 1, "");
	}

	// Take new log and quit
	if (comp_log)
	{
		char col_string[8];
		sprint_unicode(col_string, sc_red);
		print_to_layout_textedit(&layout, 10, 1, "%s%s", col_string, comp_log);
		free(comp_log);
		return;
	}

	// Window
	flwindow_init_defaults(&window);
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	draw_dialog_window_fromlayout(&window, cur_wind_on, &cur_parent_area, &layout, 0);

	// Controls
	ctrl_textedit_fromlayout(&layout, 10);
}

_Thread_local rlip_t drawcalc_prog={0};

size_t drawcalc_alloc_elem()
{
	size_t i = drawcalc.symbol0_count;

	// Stop and erase everything if hitting the limit
	if (drawcalc.symbol0_count >= DRAWCALC_ELEM_LIMIT)
	{
		drawcalc.thread_on = 0;		// end the execution
		free_null(&drawcalc.symbol0);
		drawcalc.symbol0_as = 0;
		drawcalc.symbol0_count = 1;

		// Draw warning
		extern double drawcalc_set_colour(double r, double g, double b);
		extern double drawcalc_add_line(double x0, double y0, double x1, double y1, double blur);
		extern double drawcalc_add_text(double x, double y, double scale, double alig, double v0, double v1);
		drawcalc_set_colour(1., 0.05, 0.);
		drawcalc_add_line(-7., 0., 7., 0., 1.5);
		drawcalc_set_colour(1., 0.7, 0.);
		drawcalc_add_text(0., 0., 1.5, 9., 245911947980., 3589306291512.);
		return 0;
	}

	alloc_enough_mutex(&drawcalc.symbol0, drawcalc.symbol0_count+=1, &drawcalc.symbol0_as, sizeof(drawcalc_symbol_t), 1.4, &drawcalc.array_mutex);
	return i;
}

double drawcalc_set_colour(double r, double g, double b)
{
	drawcalc.colour_cur = make_colour_frgb(r, g, b, 1.);
	return 0.;
}

double drawcalc_add_line(double x0, double y0, double x1, double y1, double blur)
{
	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_line;
	struct line *s = &drawcalc.symbol0[i].symb.line;
	s->p0 = xy(x0, y0);
	s->p1 = xy(x1, y1);
	s->blur = blur;
	s->col = drawcalc.colour_cur;
	return 0.;
}

double drawcalc_add_rect(double pos_x, double pos_y, double size_x, double size_y, double off_x, double off_y)
{
	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_rect;
	struct rect *s = &drawcalc.symbol0[i].symb.rect;
	s->rect = make_rect_off( xy(pos_x, pos_y), xy(size_x, size_y), xy(off_x, off_y) );
	s->col = drawcalc.colour_cur;
	return 0.;
}

double drawcalc_add_quad(double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, double blur)
{
	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_quad;
	struct quad *s = &drawcalc.symbol0[i].symb.quad;
	s->p[0] = xy(x0, y0);
	s->p[1] = xy(x1, y1);
	s->p[2] = xy(x2, y2);
	s->p[3] = xy(x3, y3);
	s->blur = blur;
	s->col = drawcalc.colour_cur;
	return 0.;
}

double drawcalc_add_circle(double x, double y, double radius)
{
	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_circle;
	struct circle *s = &drawcalc.symbol0[i].symb.circle;
	s->pos = xy(x, y);
	s->radius = radius;
	s->col = drawcalc.colour_cur;
	return 0.;
}

double drawcalc_add_number(double x, double y, double scale, double value, double prec, double alig)
{
 	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_number;
	struct number *s = &drawcalc.symbol0[i].symb.number;
	s->pos = xy(x, y - 0.5*scale);
	s->scale = scale * (1./6.);
	s->value = value;
	s->prec = prec;
	s->alig = alig;
	s->col = drawcalc.colour_cur;
	return 0.;
}

double drawcalc_add_text(double x, double y, double scale, double alig, double v0, double v1)
{
 	size_t i = drawcalc_alloc_elem();
	drawcalc.symbol0[i].type = type_text;
	struct text *s = &drawcalc.symbol0[i].symb.text;
	s->pos = xy(x, y - 0.5*scale);
	s->scale = scale * (1./6.);
	s->alig = alig;
	s->v[0] = v0;
	s->v[1] = v1;
	s->col = drawcalc.colour_cur;
	return 0.;
}

typedef struct
{
	double *array;
	size_t count, as;
} rlip_store_array_t;

rlip_store_array_t *rlip_store = NULL;
size_t rlip_store_count=0, rlip_store_as=0;
#define RLIP_STORE_MAX_ARRAY_COUNT 100
#define RLIP_STORE_MAX_ARRAY_LEN (4<<20)

double rlip_store_free()
{
	for (int i=0; i < rlip_store_count; i++)
		free(rlip_store[i].array);
	free_null(&rlip_store);
	rlip_store_count = rlip_store_as = 0;
	return 0.;
}

double rlip_store_val(int64_t store_id, int64_t store_index, double v)
{
	if (store_id >= RLIP_STORE_MAX_ARRAY_COUNT || store_id < 0)
		return -1.;

	if (store_index >= RLIP_STORE_MAX_ARRAY_LEN || store_index < 0)
		return -2.;

	// Enlarge as needed
	rlip_store_count = MAXN(rlip_store_count, store_id+1);
	alloc_enough(&rlip_store, rlip_store_count, &rlip_store_as, sizeof(rlip_store_array_t), 1.4);
	rlip_store[store_id].count = MAXN(rlip_store[store_id].count, store_index+1);
	alloc_enough(&rlip_store[store_id].array, rlip_store[store_id].count, &rlip_store[store_id].as, sizeof(double), 1.1);

	// Store value
	rlip_store[store_id].array[store_index] = v;
	return 0.;
}

double rlip_retrieve_val(int64_t store_id, int64_t store_index)
{
	if (store_id >= rlip_store_count || store_id < 0)
		return NAN;

	if (store_index >= rlip_store[store_id].count || store_index < 0)
		return NAN;

	return rlip_store[store_id].array[store_index];
}

void drawcalc_prog_init(drawcalc_t *d, int make_log)
{
	buffer_t comp_log={0};
	rlip_inputs_t inputs[] = {
		RLIP_FUNC,
		{"colour", drawcalc_set_colour, "fdddd"}, 
		{"line", drawcalc_add_line, "fdddddd"}, 
		{"rect", drawcalc_add_rect, "fddddddd"}, 
		{"quad", drawcalc_add_quad, "fdddddddddd"}, 
		{"circle", drawcalc_add_circle, "fdddd"}, 
		{"number", drawcalc_add_number, "fddddddd"}, 
		{"text", drawcalc_add_text, "fddddddd"}, 
		{"store_clear", rlip_store_free, "fd"}, 
		{"store", rlip_store_val, "fdiid"}, 
		{"load", rlip_retrieve_val, "fdii"}, 
		{"angle", &d->angle_v, "pd"}, {"time", &d->time_v, "pd"}, {"k0", &d->k[0], "pd"}, {"k1", &d->k[1], "pd"}, {"k2", &d->k[2], "pd"}, {"k3", &d->k[3], "pd"}, {"k4", &d->k[4], "pd"}, 
		{"xor", xor_double, "fddd"}, {"cos_tr_d2", fastcos_tr_d2, "fdd"},
		{"cost_of_factor", estimate_cost_of_mul_factor, "fdd"},
	};

	// Compilation
	free_rlip(&drawcalc_prog);
	drawcalc_prog = rlip_compile(d->expr_string, inputs, sizeof(inputs)/sizeof(*inputs), 0, make_log ? &comp_log : NULL);
	free_null(&d->expr_string);
	drawcalc_prog.exec_on = &d->thread_on;

	if (make_log)
	{
		drawcalc_compilation_log(comp_log.buf ? comp_log.buf : (uint8_t *) make_string_copy(""));

		// Decompilation
		buffer_t decomp = rlip_decompile(&drawcalc_prog);
		fprintf_rl(stdout, "Decompilation:\n%s\n", decomp.buf);
		free_buf(&decomp);
	}
}

int drawcalc_thread(drawcalc_t *d)
{
	int inputs_changed;
	double rad, th;
	static double time0=NAN, time1;

 	drawcalc_prog_init(d, 1);

	do
	{
		// Blank the array
		d->symbol0_count = 0;

		d->angle_v = d->angle_next;
		d->time_v = d->time_next;

		time1 = get_time_hr();

		// Increment time
		if (d->animation)
		{
			if (isnan(time0) || time1 - time0 > 1.)
				time0 = time1;

			d->time_next += (time1 - time0) * d->time_rate_v;
			d->time_v = d->time_next;
		}

		time0 = time1;

		d->colour_cur = make_colour_frgb(3., -1., 2., 1.);

		// Compute all symbols once
		rlip_execute_opcode(&drawcalc_prog);

		// Copy symbols
		rl_mutex_lock(&d->array_mutex);
		d->symbol1_count = d->symbol0_count;
		d->symbol1 = realloc(d->symbol1, d->symbol1_count * sizeof(drawcalc_symbol_t));
		memcpy(d->symbol1, d->symbol0, d->symbol1_count * sizeof(drawcalc_symbol_t));
		rl_mutex_unlock(&d->array_mutex);

		// Check loop flag and reset it atomically
		inputs_changed = rl_atomic_get_and_set(&d->inputs_changed, 0);
		if (d->animation)
			inputs_changed = 1;
	} while (inputs_changed && d->thread_on);

	// End thread
	free_rlip(&drawcalc_prog);
	d->thread_on = 2;

	return 0;
}

void drawcalc_form(char **form_string, int *form_ret, int *comp_log_detached)
{
	static int init=1;

	static flwindow_t window={0};
	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"elem 0", "type none", "label Formula", "pos	0	0", "dim	5;2	9;8", "off	0	1", "",
		"elem 1", "type none", "label Formula", "pos	0	0", "dim	5;2	8;6", "off	0	1", "",
		"elem 10", "type textedit", "pos	0;3	-0;9", "dim	4;8	7;6", "off	0	1", "",
		"elem 20", "type none", "link_pos_id 10", "pos	0	-7;8", "dim	4;8	1", "off	0	1", "",
	};

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Drawing calc formula");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	flwindow_init_defaults(&window);
	window.pinned_sm_preset = 1.8;
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	draw_dialog_window_fromlayout(&window, cur_wind_on, &cur_parent_area, &layout, *comp_log_detached);

	if (init)
	{
		init = 0;

		get_textedit_fromlayout(&layout, 10)->first_click_no_sel = 1;
		get_textedit_fromlayout(&layout, 10)->edit_mode = te_mode_full;
		get_textedit_fromlayout(&layout, 10)->scroll_mode = 1;
		get_textedit_fromlayout(&layout, 10)->scroll_mode_scale_def = 200.;
		print_to_layout_textedit(&layout, 10, 1, "");	// default formula
	}

	// Controls
	*form_ret = ctrl_textedit_fromlayout(&layout, 10);
	*form_string = get_textedit_string_fromlayout(&layout, 10);

	// Sub-window
	window_set_parent_area(drawcalc_compilation_log, NULL, gui_layout_elem_comp_area_os(&layout, 20, XY0));
}

void drawcalc_var_window(drawcalc_t *d)
{
	int i, ret;

	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"elem 0", "type none", "label Variables", "pos	0	0", "dim	2;11	4;6", "off	0	1", "",
		"elem 20", "type knob", "label Angle", "knob 0 0 1 linear %.3f", "pos	0;4	-0;10", "dim	1", "off	0	1", "",
		"elem 100", "type knob", "label k0", "knob -1e4 0 1e4 tan", "knob_arg 4", "pos	1;7	-0;10", "dim	1", "off	0	1", "",
		"elem 110", "type knob", "label k1", "knob -1e4 0 1e4 tan", "knob_arg 4", "link_pos_id 100", "pos	-1;3	-1;2", "dim	1", "off	0	1", "",
		"elem 120", "type knob", "label k2", "knob -1e4 0 1e4 tan", "knob_arg 4", "link_pos_id 110", "pos	1;3	0", "dim	1", "off	0	1", "",
		"elem 130", "type knob", "label k3", "knob -1e4 0 1e4 tan", "knob_arg 4", "link_pos_id 120", "pos	-1;3	-1;2", "dim	1", "off	0	1", "",
		"elem 140", "type knob", "label k4", "knob -1e4 0 1e4 tan", "knob_arg 4", "link_pos_id 130", "pos	1;3	0", "dim	1", "off	0	1", "",
	};

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Drawing calc variables");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	static flwindow_t window={0};
	flwindow_init_defaults(&window);
	window.pinned_offset_preset = xy(1e9, 4.);
	window.pinned_sm_preset = 1.1;
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	draw_dialog_window_fromlayout(&window, cur_wind_on, &cur_parent_area, &layout, 0);

	// Controls
	set_knob_circularity_fromlayout(1, &layout, 20);
	if (ctrl_knob_fromlayout(&d->angle_next, &layout, 20))
		rl_atomic_store_i32(&d->inputs_changed, 1);

	for (i=0; i < 5; i++)
		if (ctrl_knob_fromlayout(&d->k[i], &layout, 100+i*10))	// FIXME the knobs directly update into the thread which makes cool artifacts
			rl_atomic_store_i32(&d->inputs_changed, 1);
}

void drawcalc_time_window(drawcalc_t *d)
{
	int i, ret;

	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"elem 0", "type none", "label Time", "pos	0	0", "dim	2;11	2;10", "off	0	1", "",
		"elem 10", "type knob", "label Time", "knob -1e+300 0 1e+300 tan %.3g", "knob_arg 10", "pos	0;4	-0;10", "dim	1", "off	0	1", "",
		"elem 20", "type knob", "label Time rate", "knob -10000 1 10000 tan %.3g", "knob_arg 0.5", "link_pos_id 10.rt", "pos	0;3	0", "dim	1", "off	0	1", "",
		"elem 30", "type checkbox", "label Animate", "link_pos_id 10.lb", "pos	0;3	-0;3", "dim	1;9	0;5", "off	0	1", "",
	};

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Drawing calc time");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	static flwindow_t window={0};
	flwindow_init_defaults(&window);
	window.pinned_offset_preset = xy(1e9, 4.);
	window.pinned_sm_preset = 1.1;
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	draw_dialog_window_fromlayout(&window, cur_wind_on, &cur_parent_area, &layout, 0);

	// Controls
	if (ctrl_knob_fromlayout(&d->time_next, &layout, 10))
		rl_atomic_store_i32(&d->inputs_changed, 1);

	ctrl_knob_fromlayout(&d->time_rate_v, &layout, 20);
	ctrl_checkbox_fromlayout(&d->animation, &layout, 30);	
	if (d->animation)
		rl_atomic_store_i32(&d->inputs_changed, 1);
}

void drawcalc_window(drawcalc_t *d, char **form_string, int *form_ret, int *calc_form_detached, int *calc_var_detached, int *calc_time_detached)
{
	static int init=1;
	int i;
	static xyi_t dim;
	char *path;

	// Layout
	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"v 1	0;3	0", "v 2	0	-0;2", "",
		"elem 0", "type none", "label Drawing calculator parameters", "pos	-3;1	-1", "dim	5;9	6;5", "off	0	1", "",
		"elem 1", "type rect", "label Drawing calculator parameters", "pos	0;1	-1", "dim	2;7	6;5", "off	0	1", "",
		"elem 10", "type rect", "pos	0;1	-1;9", "dim	2;11	5;5", "off	1", "",
		"elem 20", "type rect", "link_pos_id 10.rt", "pos	v1", "dim	2;1	3;3", "off	0	1", "",
		"elem 30", "type rect", "link_pos_id 20._b", "pos	v2", "dim	2;1	2", "off	0	1", "",
	};

	gui_layout_init_pos_scale(&layout, add_xy(zc.limit_u, neg_y(set_xy(0.125))), 1.5, XY0, 0);
	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Drawing calc");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	static flwindow_t window={0};
	flwindow_init_defaults(&window);
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	flwindow_init_pinned(&window);
	if (*calc_form_detached==0 | *calc_var_detached==0 | *calc_time_detached==0)
		draw_dialog_window_fromlayout(&window, NULL, NULL, &layout, *calc_form_detached);

	// Formula processing
	d->recalc |= *form_ret==1 || *form_ret==4 || (d->inputs_changed && d->thread_on != 1);
	if (d->recalc)
	{
		d->recalc = 0;
		d->inputs_changed = 0;

		// Stop thread
		d->thread_on = 0;
		rl_thread_join_and_null(&d->thread_handle);

		// Prepare data
		d->expr_string = make_string_copy(*form_string);

		// Create thread
		d->thread_on = 1;
		rl_thread_create(&d->thread_handle, drawcalc_thread, d);
	}

	if (d->thread_on == 2)
		d->thread_on = 3;

	// Sub-windows
	window_set_parent_area(drawcalc_form, NULL, gui_layout_elem_comp_area_os(&layout, 10, XY0));
	window_set_parent_area(drawcalc_var_window, NULL, gui_layout_elem_comp_area_os(&layout, 20, XY0));
	window_set_parent_area(drawcalc_time_window, NULL, gui_layout_elem_comp_area_os(&layout, 30, XY0));
}

void drawing_calculator()
{
	static int init = 1;
	static rect_t im_display_rect={0};
	drawcalc_t *d = &drawcalc;
	static int calc_form_detached=0, comp_log_detached=0, calc_var_detached=0, calc_time_detached=0;
	static char *form_string=NULL;
	static int form_ret=0;
	static ctrl_resize_rect_t range_resize_state={0};

	double now = get_time_hr();

	if (init)
	{
		init = 0;

		rl_mutex_init(&drawcalc.array_mutex);

		d->angle_next = NAN;
		d->time_next = NAN;
		d->time_rate_v = NAN;
	}

	// Symbol drawing
	rl_mutex_lock(&d->array_mutex);
	for (int is=0; is < d->symbol1_count; is++)
		switch (d->symbol1[is].type)
		{
			case type_line:
			{
				struct line *s = &drawcalc.symbol1[is].symb.line;
				draw_line_thin(sc_xy(s->p0), sc_xy(s->p1), sqrt(sq(s->blur*zc.scrscale) + sq(drawing_thickness)), frgb_to_col(s->col), blend_add, 1.);
				break;
			}

			case type_rect:
			{
				struct rect *s = &drawcalc.symbol1[is].symb.rect;
				draw_rect_full(sc_rect(s->rect), drawing_thickness, frgb_to_col(s->col), blend_add, 1.);
				break;
			}

			case type_quad:
			{
				struct quad *s = &drawcalc.symbol1[is].symb.quad;
				draw_polygon_wc(s->p, 4, sqrt(sq(s->blur*zc.scrscale) + sq(drawing_thickness)), frgb_to_col(s->col), blend_add, 1.);
				break;
			}

			case type_circle:
			{
				struct circle *s = &drawcalc.symbol1[is].symb.circle;
				draw_circle(FULLCIRCLE, sc_xy(s->pos), s->radius*zc.scrscale, drawing_thickness, frgb_to_col(s->col), blend_add, 1.);
				break;
			}

			case type_number:
			{
				struct number *s = &drawcalc.symbol1[is].symb.number;
				rect_t bounding_rect = make_rect_off(s->pos, mul_xy(xy(20., 1.), set_xy(s->scale * 6.)), xy(0.5, 0.));
				//draw_rect_full(sc_rect(bounding_rect), drawing_thickness, frgb_to_col(s->col), blend_add, 1.);
				if (check_box_on_screen(bounding_rect))
					print_to_screen(s->pos, s->scale, frgb_to_col(s->col), 1., s->alig, "%.*g", (int) s->prec, s->value);
				break;
			}

			case type_text:
			{
				struct text *s = &drawcalc.symbol1[is].symb.text;
				rect_t bounding_rect = make_rect_off(s->pos, mul_xy(xy(20., 1.), set_xy(s->scale * 6.)), xy(0.5, 0.));
				if (check_box_on_screen(bounding_rect))
				{
					// Convert base98 values to string (base98 gives 8 chars in 53 bits)
					const int base = 98, char_count = 8;
					const char base98[98] =
						"\nabcdefghijklmnopqrstuvwxyz" " _\t"	// 1 = a, 26 = z
						"0123456789"				// 30 = '0'
						".:=<>+-*/|"				// 40 - 49
						",ABCDEFGHIJKLMNOPQRSTUVWXYZ" ";!?"	// 50-79, 51 = A, 76 = Z
						"\302\260'\"()[]{}"			// 80-81 = °, 82 = ", 83 = '
						"#$&@\\^`~";				// 90-97

					char string[8*2 + 1];
					int iv, ic = 0;

					for (iv=0; iv < TEXT_VAL_COUNT; iv++)
					{
						uint64_t v = s->v[iv];

						while (v)
						{
							string[ic] = base98[v % base];
							ic++;
							v /= base;

							if (ic >= 2*char_count)
							{
								ic = 2*char_count;
								goto terminate_string;
							}
						}
					}
terminate_string:
					string[ic] = '\0';

					print_to_screen(s->pos, s->scale, frgb_to_col(s->col), 1., s->alig, "%s", string);
				}
				break;
			}
		}
	rl_mutex_unlock(&d->array_mutex);
	draw_clamp();

	// Windows
	window_register(1, drawcalc_compilation_log, NULL, RECTNAN, &comp_log_detached, 1, NULL);
	window_set_parent(drawcalc_compilation_log, NULL, drawcalc_form, NULL);
	window_register(1, drawcalc_form, NULL, RECTNAN, &calc_form_detached, 3, &form_string, &form_ret, &comp_log_detached);
	window_set_parent(drawcalc_form, NULL, drawcalc_window, NULL);
	window_register(1, drawcalc_var_window, NULL, RECTNAN, &calc_var_detached, 1, d);
	window_set_parent(drawcalc_var_window, NULL, drawcalc_window, NULL);
	window_register(1, drawcalc_time_window, NULL, RECTNAN, &calc_time_detached, 1, d);
	window_set_parent(drawcalc_time_window, NULL, drawcalc_window, NULL);

	window_register(1, drawcalc_window, NULL, RECTNAN, NULL, 6, d, &form_string, &form_ret, &calc_form_detached, &calc_var_detached, &calc_time_detached);
}

#ifndef DRAWCALC_AS_A_LIBRARY
void main_loop()
{
	sdl_main_param_t param={0};
	param.window_name = "Drawing Calculator";
	param.func = drawing_calculator;
	param.use_drawq = 1;
	param.maximise_window = 1;
	param.gui_toolbar = 1;
	rl_sdl_standard_main_loop(param);
}

int main(int argc, char *argv[])
{
	#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(main_loop, 0, 1);
	#else
	main_loop();
	#endif

	sdl_quit_actions();

	return 0;
}
#endif
