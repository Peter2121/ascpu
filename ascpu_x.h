/*
 * ascpu is the CPU statistics monitor utility for X Windows
 * Copyright (c) 1998-2000  Albert Dorofeev <Albert@tigr.net>
 * For the updates see http://www.tigr.net/
 *
 * This software is distributed under GPL. For details see LICENSE file.
 */

#ifndef _ascpu_x_h_
#define _ascpu_x_h_

void ascpu_initialize(  
                        int argc, char** argv,
			char * window_name,
                        char * display_name,
			char * mainGeometry,
                        int withdrawn,
                        int iconic,
                        int pushed_in,
			int sys_color_defined,
			char * sys_color,
			int nice_color_defined,
			char * nice_color,
			int user_color_defined,
			char * user_color,
			int idle_color_defined,
			char * idle_color);
void ascpu_update();
void ascpu_redraw();
void ascpu_cleanup();

#endif

