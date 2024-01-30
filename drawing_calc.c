#include "rl.h"

typedef struct
{
	xy_t pos;
	double radius;
	frgb_t col;
} drawcalc_symbol_t;

typedef struct
{
	volatile int thread_on;
	volatile int32_t inputs_changed;
	rl_thread_t thread_handle;
	char *expr_string;
	double angle_v, k[5];
	double angle_next;

	drawcalc_symbol_t *symbol0, *symbol1;
	size_t symbol0_count, symbol0_as, symbol1_count, symbol1_as;
	rl_mutex_t array_mutex;

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

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Position calc compilation log");

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

double drawcalc_symbol_add(double x, double y, double radius, double r, double g, double b, double a)
{
	size_t i = drawcalc.symbol0_count;
	alloc_enough_mutex(&drawcalc.symbol0, drawcalc.symbol0_count+=1, &drawcalc.symbol0_as, sizeof(drawcalc_symbol_t), 1.4, &drawcalc.array_mutex);
	drawcalc.symbol0[i].pos = xy(x, y);
	drawcalc.symbol0[i].radius = radius;
	drawcalc.symbol0[i].col = make_colour_frgb(r, g, b, a);

	return 0.;
}

void drawcalc_prog_init(drawcalc_t *d, int make_log)
{
	buffer_t comp_log={0};
	rlip_inputs_t inputs[] = {
		RLIP_FUNC, {"symb", drawcalc_symbol_add, "fdddddddd"}, {"angle", &d->angle_v, "pd"},
		{"k0", &d->k[0], "pd"}, {"k1", &d->k[1], "pd"}, {"k2", &d->k[2], "pd"}, {"k3", &d->k[3], "pd"}, {"k4", &d->k[4], "pd"}, 
		{"xor", xor_double, "fddd"}, {"cos_tr_d2", fastcos_tr_d2, "fdd"},
	};

	// Compilation
	free_rlip(&drawcalc_prog);                                                        // ↓ TODO set to 0 so there's no return needed
	drawcalc_prog = rlip_compile(d->expr_string, inputs, sizeof(inputs)/sizeof(*inputs), 1, make_log ? &comp_log : NULL);
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

	drawcalc_prog_init(d, 1);

	do
	{
		// Blank the array
		d->symbol0_count = 0;

		d->angle_v = d->angle_next;

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
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	draw_dialog_window_fromlayout(&window, cur_wind_on, &cur_parent_area, &layout, *comp_log_detached);

	if (init)
	{
		init = 0;

		get_textedit_fromlayout(&layout, 10)->first_click_no_sel = 1;
		get_textedit_fromlayout(&layout, 10)->edit_mode = te_mode_full;
		get_textedit_fromlayout(&layout, 10)->scroll_mode = 1;
		get_textedit_fromlayout(&layout, 10)->scroll_mode_scale_def = 32. * 4.5;
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
	static double stride_power_v=NAN;

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

	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Pos calc variables");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	static flwindow_t window={0};
	flwindow_init_defaults(&window);
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

void drawcalc_window(drawcalc_t *d, char **form_string, int *form_ret, int *calc_form_detached)
{
	static int init=1, fit_diag_on=0;
	int i;
	static xyi_t dim;
	char *path;

	// Layout
	static gui_layout_t layout={0};
	const char *layout_src[] = {
		"v 1	0;3	0", "v 2	0	-0;2", "",
		"elem 0", "type none", "label Position calculator parameters", "pos	-3;1	-1", "dim	5;9	6;5", "off	0	1", "",
		"elem 1", "type none", "label Position calculator parameters", "pos	0;1	-1", "dim	4;9	6;5", "off	0	1", "",
		"elem 10", "type rect", "pos	0;1	-1;9", "dim	2;11	5;5", "off	1", "",
		"elem 20", "type rect", "link_pos_id 80._b", "pos	v2", "dim	2;1	3;3", "off	0	1", "",
		"elem 80", "type rect", "link_pos_id 10.rt", "pos	v1", "dim	2;1	2", "off	0	1", "",
	};

	gui_layout_init_pos_scale(&layout, add_xy(zc.limit_u, neg_y(set_xy(0.125))), 1.5, XY0, 0);
	make_gui_layout(&layout, layout_src, sizeof(layout_src)/sizeof(char *), "Position calc");

	if (mouse.window_minimised_flag > 0)
		return;

	// Window
	static flwindow_t window={0};
	flwindow_init_defaults(&window);
	window.bg_opacity = OPACITY;
	window.shadow_strength = 0.5*window.bg_opacity;
	flwindow_init_pinned(&window);
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
}

void drawing_calculator()
{
	static int init = 1;
	static rect_t im_display_rect={0};
	drawcalc_t *d = &drawcalc;
	static int calc_form_detached=0, comp_log_detached=0, calc_var_detached=0;
	static char *form_string=NULL;
	static int form_ret=0;
	static ctrl_resize_rect_t range_resize_state={0};

	double now = get_time_hr();

	if (init)
	{
		init = 0;

		rl_mutex_init(&drawcalc.array_mutex);

		d->angle_next = NAN;
	}

	// Symbol drawing
	rl_mutex_lock(&d->array_mutex);
	for (int is=0; is < d->symbol1_count; is++)
		draw_circle(FULLCIRCLE, sc_xy(d->symbol1[is].pos), d->symbol1[is].radius*zc.scrscale, drawing_thickness, frgb_to_col(d->symbol1[is].col), blend_add, 1.);
	rl_mutex_unlock(&d->array_mutex);
	draw_clamp();

	// Windows
	window_register(1, drawcalc_compilation_log, NULL, RECTNAN, &comp_log_detached, 1, NULL);
	window_set_parent(drawcalc_compilation_log, NULL, drawcalc_form, NULL);
	window_register(1, drawcalc_form, NULL, RECTNAN, &calc_form_detached, 3, &form_string, &form_ret, &comp_log_detached);
	window_set_parent(drawcalc_form, NULL, drawcalc_window, NULL);
	window_register(1, drawcalc_var_window, NULL, RECTNAN, &calc_var_detached, 1, d);
	window_set_parent(drawcalc_var_window, NULL, drawcalc_window, NULL);

	window_register(1, drawcalc_window, NULL, RECTNAN, NULL, 4, d, &form_string, &form_ret, &calc_form_detached);
}

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
