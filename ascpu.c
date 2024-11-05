/*
 * ascpu is the CPU statistics monitor utility for X Windows
 * Copyright (c) 1998-2005  Albert Dorofeev <albert@tigr.net>
 * For the updates see http://www.tigr.net/
 *
 * This software is distributed under GPL. For details see LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "safecopy.h"

#include "ascpu_x.h"
#include "state.h"

extern struct ascpu_state state;

/*
 * default check and update intervals in microseconds
 *      x11 events - every 1/100 th of a second (in mks)
 *      CPU status - every second (in sec)
 *
 *      Collect 60 samples for the average load
 *      Collect 1 samples for the history load window
 */
#define X11_INTERVAL    10000L
#define CHK_INTERVAL    1
#define AVG_SAMPLES	60
#define HIST_SAMPLES	1

#define FOR_CLICK 1

int     withdrawn = 0;
int     iconic = 0;
int     pushed_in = 1;
char    display_name[50];
char    mainGeometry[50];
char    window_name[50];

#ifdef FOR_CLICK
char	Command[255]="";
#endif

/* colors */
int sys_color_defined = 0;
char sys_color[50];
int nice_color_defined = 0;
char nice_color[50];
int user_color_defined = 0;
char user_color[50];
int idle_color_defined = 0;
char idle_color[50];

void defaults()
{
	state.update_interval = CHK_INTERVAL;
	state.avg_samples = AVG_SAMPLES;
	state.hist_samples = HIST_SAMPLES;
	state.no_nice = 0;
	state.cpu_number = -1;
	safecopy(state.proc_stat_filename, PROC_STAT, 256);
	withdrawn = 0;
	iconic = 0; 
	pushed_in = 1;
	safecopy(window_name, "ascpu", 50);
	safecopy(display_name, "", 50);
	safecopy(mainGeometry, "", 50);
	safecopy(state.bgcolor, "#303030", 50);
	safecopy(state.fgcolor, "#20b2aa", 50);

	safecopy(sys_color, "", 50);
	safecopy(nice_color, "", 50);
	safecopy(user_color, "", 50);
	safecopy(idle_color, "", 50);
}

/* print the usage for the tool */
void usage() 
{
        printf("Usage : ascpu [options ...]\n\n");
        printf("-V              print version and exit\n");
        printf("-h -H -help     print this message\n");
	printf("-title <name>	set the window/icon title to this name\n");
        printf("-u <secs>       the update interval in seconds\n");
        printf("-cpu <num>      the CPU number to show\n");
        printf("-samples <num>  the number of samples in the average\n");
        printf("-history <num> 	the number of samples for the running history\n");
        printf("-nonice		show nice CPU time as idle time\n");
        printf("-display <name> the name of the display to use\n");
        printf("-position <xy>  position on the screen (geometry)\n");
        printf("-withdrawn      start in withdrawn shape (for WindowMaker)\n");
        printf("-iconic         start iconized\n");
        printf("-standout       standing out rather than being pushed in\n");
#ifdef FOR_CLICK
	printf("-exe <command>	execute command on mouse click\n");
#endif
	printf("-dev <device>	use the specified file as stat device\n\n");
	printf("-bg <color>	background color\n");
	printf("-fg <color>	base foreground color\n");
	printf("-sys <color>	color for system CPU time\n");
	printf("-nice <color>	color for nice CPU time\n");
	printf("-user <color>	color for user CPU time\n");
	printf("-idle <color>	color for idle CPU time\n");
        printf("\n");
        exit(0);
}       

/* print the version of the tool */
void version()
{
        printf("ascpu : AfterStep CPU usage statistics monitor version 1.11\n");
}               

void    parsecmdline(int argc, char *argv[])
{
        char    *argument;
        int     i;

	/* parse the command line */
        for (i=1; i<argc; i++) {
                argument=argv[i];
                if (argument[0]=='-') {
                        if (!strncmp(argument,"-withdrawn",10)) {
                                withdrawn=1;
                        } else if (!strncmp(argument,"-iconic",7)) {
                                iconic=1;
                        } else if (!strncmp(argument,"-standout",9)) {
                                pushed_in=0;
                        } else if (!strncmp(argument,"-nonice",7)) {
                                state.no_nice=1;
                        } else if (!strncmp(argument,"-cpu",4)) {
                                if (++i >= argc)
                                        usage();
                                state.cpu_number = atoi(argv[i]);
                                if ( state.cpu_number < 0 )
                                        state.cpu_number = -1;
				if ( state.cpu_number >= MAX_CPU ) {
					state.cpu_number = -1;
					printf("ascpu: The maximum number of CPUs supported was restricted to %d at compilation\n", MAX_CPU);
				}
                        } else if (!strncmp(argument,"-samples",8)) {
                                if (++i >= argc)
                                        usage();
                                state.avg_samples = atoi(argv[i]);
                                if ( state.avg_samples < 1 )  
                                        state.avg_samples = AVG_SAMPLES;
#ifdef FOR_CLICK
                        } else if (!strncmp(argument,"-exe",4)) {
                                if (++i >= argc)
                                        usage();
                                safecopy(Command, argv[i], 252);
				strcat(Command," &");
#endif
                        } else if (!strncmp(argument,"-history",8)) {
                                if (++i >= argc)
                                        usage();
                                state.hist_samples = atoi(argv[i]);
                                if ( state.hist_samples < 1 )  
                                        state.hist_samples = HIST_SAMPLES;
                        } else if (!strncmp(argument,"-position",9)) {
                                if (++i >= argc)
                                        usage();
                                safecopy(mainGeometry, argv[i], 50);
                        } else if (!strncmp(argument,"-title",6)) {
                                if (++i >= argc)
                                        usage();
                                safecopy(window_name, argv[i], 50);
                        } else if (!strncmp(argument,"-display",8)) {
                                if (++i >= argc)
                                        usage();
                                safecopy(display_name, argv[i], 50);
                        } else if (!strncmp(argument,"-dev",4)) {
                                if (++i >= argc)
                                        usage();
                                safecopy(state.proc_stat_filename,argv[i],256);
			} else if (!strncmp(argument,"-bg",3)) {
				if (++i >= argc)
					usage();
				safecopy(state.bgcolor, argv[i], 50);
			} else if (!strncmp(argument,"-fg",3)) {
				if (++i >= argc)
					usage();
				safecopy(state.fgcolor, argv[i], 50);
			} else if (!strncmp(argument,"-sys",4)) {
				if (++i >= argc)
					usage();
				safecopy(sys_color, argv[i], 50);
				sys_color_defined = 1;
			} else if (!strncmp(argument,"-nice",5)) {
				if (++i >= argc)
					usage();
				safecopy(nice_color, argv[i], 50);
				nice_color_defined = 1;
			} else if (!strncmp(argument,"-user",5)) {
				if (++i >= argc)
					usage();
				safecopy(user_color, argv[i], 50);
				user_color_defined = 1;
			} else if (!strncmp(argument,"-idle",5)) {
				if (++i >= argc)
					usage();
				safecopy(idle_color, argv[i], 50);
				idle_color_defined = 1;
                        } else if (!strncmp(argument,"-u",2)) {
                                if (++i >= argc)
                                        usage();
                                state.update_interval = atoi(argv[i]);
                                if ( state.update_interval < 1 )  
                                        state.update_interval = CHK_INTERVAL;
                        } else if (!strncmp(argument,"-V",2)) { 
                                version();
                                exit(0);
                        } else if (!strncmp(argument,"-H",2)) {
                                version();
                                usage();
                        } else if (!strncmp(argument,"-h",2)) {
                                version();
                                usage();
                        } else {
                                version();
                                usage();
                        }       
                } else {
                        version();
                        usage();
                }       
        }       
 
}

int main(int argc, char** argv)
{
	defaults();
	parsecmdline(argc, argv);
	ascpu_initialize(argc, argv, 
			window_name,
			display_name, 
			mainGeometry, 
			withdrawn, 
			iconic, 
			pushed_in,
			sys_color_defined,
			sys_color,
			nice_color_defined,
			nice_color,
			user_color_defined,
			user_color,
			idle_color_defined,
			idle_color);
	while (1) {
		ascpu_update();
		usleep(X11_INTERVAL);
	}
	return(1);
}

