/* 
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2015, Seven Du <dujinfang@gmail.com>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Seven Du <dujinfang@gmail.com>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * 
 *
 * mod_x11 -- X11 Functions
 *
 *
 */

#include <switch.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xproto.h>
#include <libyuv.h>

SWITCH_MODULE_LOAD_FUNCTION(mod_x11_load);
SWITCH_MODULE_DEFINITION(mod_x11, mod_x11_load, NULL, NULL);

typedef struct x11_context_s {
	Display *display;
	Window win;
	XEvent event;
	int screen;
	GC gc;
	int running;
} x11_context_t;

XImage *CreateTrueColorImage(Display *display, Visual *visual, switch_image_t *img)
{
    int w = img->d_w;
    int h = img->d_h;
    uint8_t *image32 = malloc(w * h * 4);
    XImage *image = NULL;

	I420ToARGB(img->planes[SWITCH_PLANE_Y], img->stride[SWITCH_PLANE_Y],
		img->planes[SWITCH_PLANE_U], img->stride[SWITCH_PLANE_U],
		img->planes[SWITCH_PLANE_V], img->stride[SWITCH_PLANE_V],
		image32, w * 4, w, h);

    image = XCreateImage(display, visual, 24, ZPixmap, 0, (char *)image32, w, h, 32, 0);
    // free(image32);
    return image;
}

static void x11_video_thread(switch_core_session_t *session, void *obj)
{
	x11_context_t *context = obj;
	switch_codec_t *codec;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_frame_t *frame;
	uint32_t width = 0, height = 0;
	uint32_t decoded_pictures = 0;
	int count = 0;
	char *msg = "blah";

	context->running = 1;

	if (!switch_channel_ready(channel)) {
		goto done;
	}

	codec = switch_core_session_get_video_read_codec(session);

	if (!codec) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Channel has no video read codec\n");
		goto done;
	}

	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_READ);
	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_WRITE);

	while (switch_channel_ready(channel)) {
		switch_status_t status = switch_core_session_read_video_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0);

		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}

		if (!count || ++count == 101) {
			switch_core_session_request_video_refresh(session);
			count = 1;
		}
			

		if (frame && frame->datalen > 0) {
			// switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
		} else {
			continue;
		}

		if (switch_test_flag(frame, SFF_CNG) || frame->datalen < 3) {
			continue;
		}

		if (frame->img) {
			if (frame->img->d_w > 0 && !width) {
				width = frame->img->d_w;
				switch_channel_set_variable_printf(channel, "video_width", "%d", width);
			}

			if (frame->img->d_h > 0 && !height) {
				height = frame->img->d_h;
				switch_channel_set_variable_printf(channel, "video_height", "%d", height);
			}

			decoded_pictures++;
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "picture#%d %dx%d\n", decoded_pictures, frame->img->d_w, frame->img->d_h);

			if (!context->win) {
				const char *cid_name = switch_channel_get_variable(channel, "caller_id_name");
				const char *cid_number = switch_channel_get_variable(channel, "caller_id_number");
				char *cid = switch_core_session_sprintf(session, "\"%s\" <%s>", cid_name, cid_number);

				context->win = XCreateSimpleWindow(context->display,
					RootWindow(context->display, context->screen),
					0, 0, width, height, 1,
					BlackPixel(context->display, context->screen),
					WhitePixel(context->display, context->screen));
				XSetStandardProperties(context->display, context->win, cid, "HI!", None, NULL, 0, NULL);
				XSelectInput(context->display, context->win, ExposureMask | KeyPressMask);
				XMapWindow(context->display, context->win);
				context->gc = DefaultGC(context->display, context->screen);
			}

			if (XPending(context->display)) {
				XNextEvent(context->display, &context->event);
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "event: %d\n", context->event.type);
				if (context->event.type == Expose) {
					// if (context->event.xexpose.count == 0) redraw();
					XFillRectangle(context->display, context->win, DefaultGC(context->display, context->screen), 20, 20, 10, 10);
					XDrawString(context->display, context->win, DefaultGC(context->display, context->screen), 10, 50, msg, strlen(msg));
				// } else if(context->event.type == ClientMessage && context->event.xclient.data.l[0] == wmDeleteMessage) {
				    // XDestroyWindow(context->display, context->event.xclient.window);
			    }
				// if (context->event.type == KeyPress) break;
			}

			{
				XImage *ximage;
				Visual *visual=DefaultVisual(context->display, 0);
				ximage=CreateTrueColorImage(context->display, visual, frame->img);
				XPutImage(context->display, context->win, DefaultGC(context->display, 0), ximage, 0, 0, 0, 0, frame->img->d_w, frame->img->d_h);
				XDestroyImage(ximage);
			}

			{
				Window root = DefaultRootWindow(context->display);
				XImage *image = XGetImage(context->display, root, 0, 0 , frame->img->d_w, frame->img->d_h, AllPlanes, ZPixmap);

				switch_assert(image);

				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "width: %d height: %d xoffset: %d format: %d depth: %d\n",
					image->width, image->height, image->xoffset, image->format, image->depth);

				ARGBToI420((uint8_t *)image->data, image->width * 4,
					frame->img->planes[SWITCH_PLANE_Y], frame->img->stride[SWITCH_PLANE_Y],
					frame->img->planes[SWITCH_PLANE_U], frame->img->stride[SWITCH_PLANE_U],
					frame->img->planes[SWITCH_PLANE_V], frame->img->stride[SWITCH_PLANE_V],
               		frame->img->d_w, frame->img->d_h);

				switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
			}
		}
	}

 done:

 	context->running = 0;
	return;
}

SWITCH_STANDARD_APP(x11_video_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	x11_context_t context = { 0 };
	const char *moh = switch_channel_get_hold_music(channel);
	// char *msg = "Hello FreeSWITCH";

	if (zstr(moh)) {
		moh = "silence_stream://-1";
	}

	context.display = XOpenDisplay(data);
	if (context.display == NULL) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot open display\n");
		goto end;
	}
	context.screen = DefaultScreen(context.display);

	switch_channel_answer(channel);
	switch_core_session_request_video_refresh(session);

	switch_channel_set_flag(channel, CF_VIDEO_DECODED_READ);

	switch_core_media_start_video_function(session, x11_video_thread, &context);

	switch_ivr_play_file(session, NULL, moh, NULL);

	switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

end:

	while (context.running) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Waiting video thread\n");
		switch_yield(1000000);
	}

	if (context.display) XCloseDisplay(context.display);

	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

	switch_core_media_end_video_function(session);
	switch_core_session_video_reset(session);
}

typedef struct xcb_context_s {
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_gcontext_t gc;
	xcb_image_t *image;
	char *display;
	int running;
	switch_core_session_t *session;
} xcb_context_t;


static void create_window(xcb_context_t *context, int width, int height) {
  uint32_t mask;
  uint32_t values[2];

  xcb_void_cookie_t cookie;

  mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  values[0] = context->screen->white_pixel;
  values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;

  context->window = xcb_generate_id(context->connection);
  cookie = xcb_create_window(context->connection,
                 XCB_COPY_FROM_PARENT, context->window, context->screen->root,
                 0, 0, width, height,
                 0,
                 XCB_WINDOW_CLASS_INPUT_OUTPUT,
                 context->screen->root_visual,
                 mask, values);
  xcb_map_window(context->connection, context->window);
}

static xcb_gcontext_t create_graphics_context(xcb_context_t *context) {
  uint32_t mask;
  uint32_t values[3];

  mask = XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_GRAPHICS_EXPOSURES;
  values[0] = context->screen->black_pixel;
  values[1] = context->screen->white_pixel;
  values[2] = 0;

  context->gc = xcb_generate_id(context->connection);
  xcb_create_gc(context->connection,
        context->gc, context->window,
        mask, values);

  return context->gc;
}

static void *SWITCH_THREAD_FUNC screen_capture_thread_run(switch_thread_t *thread, void *obj)
{
	xcb_context_t *context = (xcb_context_t *)obj;
	xcb_get_image_reply_t *image;
	switch_frame_t frame = { 0 };
	uint8_t *data = NULL;
	size_t datalen;
	switch_time_t now = 0;
	switch_time_t last = 0;
	int w;
	int h;
	xcb_drawable_t drawable;
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_get_geometry_reply_t *geo;
	xcb_get_geometry_cookie_t cookie;

	connection = xcb_connect(context->display, NULL); // open another connection to avoid blocking
	if (xcb_connection_has_error(connection)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot open display\n");
		return NULL;
	}
	screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
	drawable = screen->root;

	cookie = xcb_get_geometry(connection, drawable);
	geo = xcb_get_geometry_reply(connection, cookie, NULL);

	if (geo == NULL) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "err geo\n");
		return NULL;
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "screen %dx%d\n", geo->width, geo->height);
	w = geo->width;
	h = geo->height;

	frame.packet = malloc(65535);
	frame.data = (uint8_t *)frame.packet + 12;

	switch_assert(frame.packet);

	while(context->running) {
		now = switch_time_now();
		if (now - last < 40000) {// 25 fps
			switch_yield(20000);
			continue;
		}
		last = now;

		image = xcb_get_image_reply(connection,
					xcb_get_image (connection, XCB_IMAGE_FORMAT_Z_PIXMAP, drawable,
						0, 0, w, h, ~0), NULL);
		if (image == NULL) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "err capture img\n");
			switch_yield(1000000);
			continue;
		}

		data = xcb_get_image_data(image);
		datalen = xcb_get_image_data_length(image);

		// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "img: %p, data: %p, datalen: %" SWITCH_SIZE_T_FMT "\n", image, data, datalen);

		switch_assert(datalen == w * h * 4);

		frame.img = switch_img_alloc(NULL, SWITCH_IMG_FMT_I420, w, h, 0);
		switch_assert(frame.img);

		ARGBToI420(data, w * 4,
			frame.img->planes[SWITCH_PLANE_Y], frame.img->stride[SWITCH_PLANE_Y],
			frame.img->planes[SWITCH_PLANE_U], frame.img->stride[SWITCH_PLANE_U],
			frame.img->planes[SWITCH_PLANE_V], frame.img->stride[SWITCH_PLANE_V],
       		w, h);

		switch_core_session_write_video_frame(context->session, &frame, SWITCH_IO_FLAG_NONE, 0);
	}

	if (connection) {
		xcb_disconnect(connection);
	}

	free(frame.packet);
	return NULL;
}

static void xcb_video_thread(switch_core_session_t *session, void *obj)
{
	xcb_context_t *context = obj;
	switch_codec_t *codec;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_frame_t *frame;
	uint32_t width = 0, height = 0;
	int count = 0;
	xcb_generic_event_t *e;
	switch_thread_t *capture_thread = NULL;
	switch_status_t status;

	context->running = 1;

	if (!switch_channel_ready(channel)) {
		goto done;
	}

	codec = switch_core_session_get_video_read_codec(session);

	if (!codec) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Channel has no video read codec\n");
		goto done;
	}

	if (1) { // capther thread
		switch_threadattr_t *thd_attr = NULL;

		context->session = session;
		switch_threadattr_create(&thd_attr, switch_core_session_get_pool(session));
		switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
		switch_thread_create(&capture_thread, thd_attr, screen_capture_thread_run, context, switch_core_session_get_pool(session));
	}

	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_READ);
	// switch_channel_set_flag(channel, CF_VIDEO_DEBUG_WRITE);

	while (switch_channel_ready(channel)) {
		switch_status_t status = switch_core_session_read_video_frame(session, &frame, SWITCH_IO_FLAG_NONE, 0);

		if (switch_channel_test_flag(channel, CF_BREAK)) {
			switch_channel_clear_flag(channel, CF_BREAK);
			break;
		}

		if (!SWITCH_READ_ACCEPTABLE(status)) {
			break;
		}

		if (!count || ++count == 101) {
			switch_core_session_request_video_refresh(session);
			count = 1;
		}

		if (frame && frame->datalen > 0) {
			// switch_core_session_write_video_frame(session, frame, SWITCH_IO_FLAG_NONE, 0);
		} else {
			continue;
		}

		if (switch_test_flag(frame, SFF_CNG) || frame->datalen < 3) {
			continue;
		}

		if (frame->img) {
			if (frame->img->d_w > 0 && !width) {
				width = frame->img->d_w;
				switch_channel_set_variable_printf(channel, "video_width", "%d", width);
			}

			if (frame->img->d_h > 0 && !height) {
				height = frame->img->d_h;
				switch_channel_set_variable_printf(channel, "video_height", "%d", height);
			}

			if (!context->window) {
				const char *cid_name = switch_channel_get_variable(channel, "caller_id_name");
				const char *cid_number = switch_channel_get_variable(channel, "caller_id_number");
				char *cid = switch_core_session_sprintf(session, "\"%s\" <%s>", cid_name, cid_number);

				create_window(context, frame->img->d_w, frame->img->d_h);;
				create_graphics_context(context);
				xcb_change_property(context->connection, XCB_PROP_MODE_REPLACE, context->window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(cid), cid);
			}

			while ((e = xcb_poll_for_event(context->connection))) {
				// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "response: 0x%0x\n", e->response_type);
				switch (e->response_type & ~0x80) {
				case XCB_EXPOSE: {
					/* Handle the Expose event type */
					// xcb_expose_event_t *ev = (xcb_expose_event_t *)e;

					break;
				}
				case XCB_BUTTON_PRESS: {
					/* Handle the ButtonPress event type */
					// xcb_button_press_event_t *ev = (xcb_button_press_event_t *)e;
					break;
				}
				default:
					/* Unknown event type, ignore it */
					// switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "default response type: %x\n", e->response_type);
					break;
    			}
    			/* Free the Generic Event */
    			free (e);
			}

			if (1) {
				int w = frame->img->d_w;
				int h = frame->img->d_h;
				uint8_t *buffer = malloc(w * h * 4);
				switch_assert(buffer);

				memset(buffer, 192, w * h);

				I420ToARGB(frame->img->planes[SWITCH_PLANE_Y], frame->img->stride[SWITCH_PLANE_Y],
					frame->img->planes[SWITCH_PLANE_U], frame->img->stride[SWITCH_PLANE_U],
					frame->img->planes[SWITCH_PLANE_V], frame->img->stride[SWITCH_PLANE_V],
					buffer, w * 4, w, h);

				context->image = xcb_image_create_native(context->connection,
			 		w, h,
			 		XCB_IMAGE_FORMAT_Z_PIXMAP,
			 		24,
			 		NULL,
			 		w * h * 4,
			 		buffer);

				switch_assert(context->image);

				xcb_image_put(context->connection,
					context->window, context->gc, context->image, 0, 0, 0);

				if (0) {
					xcb_rectangle_t r = { 20, 20, 60, 60 };
					xcb_poly_fill_rectangle(context->connection, context->window, context->gc, 1, &r);
				}

			    xcb_flush(context->connection);
			    xcb_image_destroy(context->image);
			    free(buffer);
			}
		}
	}

 done:

 	context->running = 0;

 	if (capture_thread) switch_thread_join(&status, capture_thread);
	return;
}

SWITCH_STANDARD_APP(xcb_video_function)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	xcb_context_t context = { 0 };
	const char *moh = switch_channel_get_hold_music(channel);

	if (zstr(moh)) {
		moh = "silence_stream://-1";
	}

	context.display = data;
	context.connection = xcb_connect(context.display, NULL);
	if (xcb_connection_has_error(context.connection)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Cannot open display\n");
		goto end;
	}
	context.screen = xcb_setup_roots_iterator(xcb_get_setup(context.connection)).data;
	xcb_flush(context.connection);

	switch_channel_answer(channel);
	switch_core_session_request_video_refresh(session);

	switch_channel_set_flag(channel, CF_VIDEO_DECODED_READ);

	switch_core_media_start_video_function(session, xcb_video_thread, &context);

	switch_ivr_play_file(session, NULL, moh, NULL);

	switch_channel_set_variable(channel, SWITCH_PLAYBACK_TERMINATOR_USED, "");

end:

	while (context.running) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Waiting video thread\n");
		switch_yield(1000000);
	}

 	if (context.window) {
 		xcb_destroy_window(context.connection, context.window);
 	}

	if (context.connection) {
		xcb_disconnect(context.connection);
	}

	switch_channel_set_variable(channel, SWITCH_CURRENT_APPLICATION_RESPONSE_VARIABLE, "OK");

	switch_core_media_end_video_function(session);
	switch_core_session_video_reset(session);
}


SWITCH_MODULE_LOAD_FUNCTION(mod_x11_load)
{
	switch_application_interface_t *app_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_APP(app_interface, "x11_video", "show video on x11", "show video on x11", x11_video_function, "[display]", SAF_NONE);
	SWITCH_ADD_APP(app_interface, "xcb_video", "show video on x11 (using xcb)", "show video on x11", xcb_video_function, "[display]", SAF_NONE);

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* For Emacs:
 * Local Variables:
 * mode:c
 * indent-tabs-mode:t
 * tab-width:4
 * c-basic-offset:4
 * End:
 * For VIM:
 * vim:set softtabstop=4 shiftwidth=4 tabstop=4 noet:
 */
