/*
 * ascpu is the CPU statistics monitor utility for X Windows
 * Copyright (c) 1998-2005  Albert Dorofeev <albert@tigr.net>
 * For the updates see http://www.tigr.net/
 *
 * This software is distributed under GPL. For details see LICENSE file.
 */

#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#ifdef	__FreeBSD__
#include <nlist.h>
#include <fcntl.h>
#include <kvm.h>
#include <sys/types.h>
#include <err.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#endif

#ifdef __hpux__
#include <sys/pstat.h>
#include <sys/dk.h>
#endif

#ifdef _AIX32   /* AIX > 3.1 */
#include <nlist.h>
#include <sys/param.h>
#include <sys/sysinfo.h>
#endif /* _AIX32 */

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/Xatom.h>

#include "x_color.h"
#include "background.xpm"
#include "state.h"

#define FOR_CLICK 1

#ifdef FOR_CLICK
extern char Command[255];
#endif

struct ascpu_state state;

/* nice idea from ascd */
typedef struct _XpmIcon {
    Pixmap pixmap;
    Pixmap mask;
    XpmAttributes attributes;
} XpmIcon;

XpmIcon backgroundXpm;

/* X windows related global variables */
Display * mainDisplay = 0;      /* The display we are working on */
Window Root;                    /* The root window of X11 */
Window mainWindow;              /* Application window */
Window iconWindow;              /* Icon window */
XGCValues mainGCV;              /* graphics context values */
GC mainGC;                      /* Graphics context */
Atom wm_delete_window;
Atom wm_protocols;

Pixel back_pix, fore_pix;

/* background pixmap colors */
char bgpixmap_color[3][50];

/* pixels we need */
Pixel pix[4];

/* last time we updated */
time_t last_time = 0;

/* requests for update */
int update_request = 0;

#ifdef	__FreeBSD__
static int		cp_time_mib[2];
static int		cp_times_mib[2];
static kvm_t		*kd;
static struct nlist	nlst[] = {
	{"_cp_time"}, {0}
};
#endif

#ifdef	__linux__
char	proc_extended = 0;
#endif

#ifdef _AIX32   /* AIX > 3.1 */
/* Kernel memory file */
#define KMEM_FILE "/dev/kmem"
/* Descriptor of kernel memory file */
static int  kmem;
/* Offset of sysinfo structure in kernel memory file */
static long sysinfo_offset;
/* Structure to access kernel symbol table */
static struct nlist namelist[] = {
  { {"sysinfo"}, 0, 0, {0}, 0, 0 },
  { {0},         0, 0, {0}, 0, 0 }
};
#endif /* _AIX32 */

/*
 * The information over the CPU load is always kept in 4 variables
 * The order is:
 * 	user
 * 	nice
 * 	system
 *	interrupt(FreeBSD specific)
 * 	idle
 */
struct cpu_load_struct {
	unsigned long int load[5];
};

/*
 * The structure for the line sizes - the same as the
 * CPU structure only in pixels - ready to draw
 */
struct line_size_struct {
	unsigned short int len[4];
};
/* last time and just now read values */
struct cpu_load_struct last = {{0, 0, 0, 0}};
struct cpu_load_struct fresh = {{0, 0, 0, 0, 0}};

/* The running history window values: load, calculated lines, counter */
struct cpu_load_struct running_last = {{0, 0, 0, 0}};
struct line_size_struct running_lines = {{0, 0, 0, 0}};
unsigned long int running_counter = 0;

/* the average load gadget values */
struct cpu_load_struct average_diff = {{0, 0, 0, 0}};
struct line_size_struct average_lines = {{0, 0, 0, 0}};
struct { 
	float load[4]; 
} average = {{0, 0, 0, 0}};
struct cpu_load_struct *average_history = 0;
unsigned long int average_counter = 0;
unsigned long int average_ptr = 0;


/*
 * This function clears up all X related
 * stuff and exits. It is called in case
 * of emergencies .
 */             

void ascpu_cleanup()
{
        if ( mainDisplay ) {
                XCloseDisplay(mainDisplay);
        }
	if ( average_history )
		free( average_history );
        exit(0);
}


void update_running()
{
	unsigned short total;
	float factor;
	struct cpu_load_struct running_diff;
	int i, last_offset, new_offset;
        long diff_buf;

	++running_counter;
	if ( running_counter < state.hist_samples )
		return;

	total = 0;
	for (i=0; i<4; ++i) {
		diff_buf = fresh.load[i] - running_last.load[i];
                running_diff.load[i] = (diff_buf < 0) ? 0:diff_buf;
		total += running_diff.load[i];
	}
	if (total) {
		factor = ((float)(backgroundXpm.attributes.height - 2)) / total;
		for ( i=0; i<4; ++i )
			running_lines.len[i] = 
				rint((float)running_diff.load[i] * factor);
		/* Make sure we have the exact number of pixels to
		 * draw - errors of rounding may make up for an extra
		 * or a missing pixel */
		while ( ( running_lines.len[0] + running_lines.len[1] +
			running_lines.len[2] + running_lines.len[3] ) >
				(backgroundXpm.attributes.height - 2) ) {
			if ( running_lines.len[0] )
				--running_lines.len[0];
			else if ( running_lines.len[1] )
				--running_lines.len[1];
			else if ( running_lines.len[2] )
				--running_lines.len[2];
			else
				--running_lines.len[3];
		}
		while ( ( running_lines.len[0] + running_lines.len[1] +
			running_lines.len[2] + running_lines.len[3] ) <
				(backgroundXpm.attributes.height - 2) ) {
			++running_lines.len[3];
		}
	} else {
		/* Just in case something goes wrong *shrug* */
		running_lines.len[0] = running_lines.len[1] =
		running_lines.len[2] = 0;
		running_lines.len[3] = backgroundXpm.attributes.height - 2;
	}

#ifdef DEBUG
	printf("Running :	%d	%d	%d	%d	=%d\n",
			running_lines.len[0], running_lines.len[1],
			running_lines.len[2], running_lines.len[3],
			running_lines.len[0] + running_lines.len[1] +
			running_lines.len[2] + running_lines.len[3] );
#endif
	running_counter = 0;
	memcpy( &running_last, &fresh, sizeof(running_last) );

	/*
	 * And now draw the lines we got into the bitmap
	 * of the background. We shift what we had there
	 * before to the left and draw an extra line
	 * that we just calculated on the right side.
	 */
        XCopyArea(
                mainDisplay,
                backgroundXpm.pixmap,
                backgroundXpm.pixmap,
                mainGC,
                7,
                1,
                (backgroundXpm.attributes.width - 8),
                (backgroundXpm.attributes.height - 2),
                6,
                1
                );
	last_offset = new_offset = 1;
	for ( i=0; i<4; ++i ) {
		mainGCV.foreground = pix[i];
		XChangeGC(
			mainDisplay,
			mainGC,
			GCForeground,
			&mainGCV
			);
		new_offset += running_lines.len[3-i];
		XDrawLine(
			mainDisplay,
			backgroundXpm.pixmap,
			mainGC,
			(backgroundXpm.attributes.width - 2),
			last_offset,
			(backgroundXpm.attributes.width - 2),
			new_offset
			);
		last_offset = new_offset;
	}
	++update_request;
}

/*
 * Calculate the average load values when the average
 * time is 1 - this is the case when we just have the
 * momentary values.
 */
void momentary_average()
{
	unsigned short total;
	float factor;
	int i;

	total = average_diff.load[0] + average_diff.load[1] +
		average_diff.load[2] + average_diff.load[3];
	if (total) {
		factor = ((float)(backgroundXpm.attributes.height - 2)) / total;
		for ( i=0; i<4; ++i )
			average_lines.len[i] = 
				rint((float)average_diff.load[i] * factor);
		/* Make sure we have the exact number of pixels to
		 * draw - errors of rounding may make up for an extra
		 * or a missing pixel */
		while ( ( average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] ) >
				(backgroundXpm.attributes.height - 2) ) {
			if ( average_lines.len[0] )
				--average_lines.len[0];
			else if ( average_lines.len[1] )
				--average_lines.len[1];
			else if ( average_lines.len[2] )
				--average_lines.len[2];
			else
				--average_lines.len[3];
		}
		while ( ( average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] ) <
				(backgroundXpm.attributes.height - 2) ) {
			++average_lines.len[3];
		}
	} else {
		/* Just in case something goes wrong *shrug* */
		average_lines.len[0] = average_lines.len[1] =
		average_lines.len[2] = 0;
		average_lines.len[3] = backgroundXpm.attributes.height - 2;
	}

#ifdef DEBUG
	printf("Average :	%d	%d	%d	%d	=%d\n",
			average_lines.len[0], average_lines.len[1],
			average_lines.len[2], average_lines.len[3],
			average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] );
#endif
}

/*
 * Calculate and update the value of the average load
 * for the past hist_samples intervals. The oldest value
 * is thrown out of the average and the new value is added.
 */
void real_average()
{
	int i;
	unsigned short total;
	float factor;

	if ( average_counter >= state.avg_samples ) {
		for ( i=0; i<4; ++i ) {
			average.load[i] = 
				(average.load[i] * average_counter -
				average_history[average_ptr].load[i]) /
				( average_counter - 1 );
		}
		--average_counter;
	}
	for ( i=0; i<4; ++i ) {
		average_history[average_ptr].load[i] = 
			average_diff.load[i];
		average.load[i] = (average.load[i] * average_counter +
				average_history[average_ptr].load[i]) /
				( average_counter + 1 );
	}
	++average_counter;
	++average_ptr;
	if ( average_ptr >= state.avg_samples ) average_ptr = 0;

	/*
	 * And now the lines for the indicator
	 */
	total = average.load[0] + average.load[1] +
		average.load[2] + average.load[3];
	if (total) {
		factor = ((float)(backgroundXpm.attributes.height - 2)) / total;
		for ( i=0; i<4; ++i )
			average_lines.len[i] = 
				rint((float)average.load[i] * factor);
		/* Make sure we have the exact number of pixels to
		 * draw - errors of rounding may make up for an extra
		 * or a missing pixel */
		while ( ( average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] ) >
				(backgroundXpm.attributes.height - 2) ) {
			if ( average_lines.len[0] )
				--average_lines.len[0];
			else if ( average_lines.len[1] )
				--average_lines.len[1];
			else if ( average_lines.len[2] )
				--average_lines.len[2];
			else
				--average_lines.len[3];
		}
		while ( ( average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] ) <
				(backgroundXpm.attributes.height - 2) ) {
			++average_lines.len[3];
		}
	} else {
		/* Just in case something goes wrong *shrug* */
		average_lines.len[0] = average_lines.len[1] =
		average_lines.len[2] = 0;
		average_lines.len[3] = backgroundXpm.attributes.height - 2;
	}

#ifdef DEBUG
	printf("Average :	%d	%d	%d	%d	=%d\n",
			average_lines.len[0], average_lines.len[1],
			average_lines.len[2], average_lines.len[3],
			average_lines.len[0] + average_lines.len[1] +
			average_lines.len[2] + average_lines.len[3] );
#endif
}

/*
 * Trigger calculation of the average value and then
 * draw the calculated lines.
 */
void update_average()
{
	int offset;
	int i;
        long diff_buf;
	for (i=0; i<4; ++i) {
		diff_buf = fresh.load[i] - last.load[i];
                average_diff.load[i] = (diff_buf < 0) ? 0:diff_buf;
	}
        if ( state.avg_samples > 1 ) {
		real_average();
	} else {
		momentary_average();
	}
	/*
	 * And now draw the line that was calculated
	 */
	offset = 1;
	for ( i=0; i<4; ++i ) {
		mainGCV.foreground = pix[i];
		XChangeGC(
			mainDisplay,
			mainGC,
			GCForeground,
			&mainGCV
			);
		XFillRectangle(
			mainDisplay,
			backgroundXpm.pixmap,
			mainGC,
			1,
			offset,
			3,
			average_lines.len[3-i]
			);
		offset += average_lines.len[3-i];
	}
	++update_request;
}

void draw_window(Window win)
{
        XCopyArea(
                mainDisplay,
                backgroundXpm.pixmap,
                win,
                mainGC,
                0,
                0,
                backgroundXpm.attributes.width,
                backgroundXpm.attributes.height,
                0,
                0
                );
}


static char buffer[MAX_CPU*40]; /* about 35 chars per line of CPU info */
static char scan_format[MAX_CPU*30]; /* 26 is correct I think but not sure */
/*
 * Prepares the format for reading the /proc/stat
 */
void prepare_format()
{
	int i;
	scan_format[0] = '\0';
#ifdef __linux__
	int fd, len, major, minor, micro;
	if ((fd = open("/proc/sys/kernel/osrelease", O_RDONLY)) == -1) {
		printf("ascpu : cannot open /proc/sys/kernel/osrelease\n");
		exit(1);
	}
	if ((len = read(fd, buffer, sizeof(buffer)-1)) < 0) {
		printf("ascpu : cannot read /proc/sys/kernel/osrelease\n");
		exit(1);
	}
	buffer[len] = '\0';
	close(fd);
	if (!sscanf(buffer, "%d.%d.%d", &major, &minor, &micro)) {
		printf("ascpu : cannot get Linux Major/Minor version\n");
		exit(1);
	}
	if (major > 2 || (major == 2 && minor > 6) || (major == 2 && minor == 6 && micro >= 10)) {
#ifdef DEBUG
		printf("Linux Kernel >= 2.6.10 detected\n");
#endif
		proc_extended = 2;
	} else if (major == 2 && minor >= 5) {
#ifdef DEBUG
		printf("Linux Kernel >= 2.5 and < 2.6.10 detected\n");
#endif
		proc_extended = 1;
	} else {
		proc_extended = 0;
#ifdef DEBUG
		printf("Linux Kernel < 2.5 detected\n");
#endif
	}
	if ( state.cpu_number < 0 ) {
		sprintf(scan_format, proc_extended?((proc_extended-1)?"cpu %%ld %%ld %%ld %%ld %%*ld %%*ld %%*ld %%*ld\n":"cpu %%ld %%ld %%ld %%ld %%*ld %%*ld %%*ld\n"):"cpu %%ld %%ld %%ld %%ld\n");
	} else {
		sprintf(scan_format, proc_extended?((proc_extended-1)?"cpu %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld\n":"cpu %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld\n"):"cpu %%*ld %%*ld %%*ld %%*ld\n");
		for (i=0; i<state.cpu_number; ++i) {
			sprintf(&scan_format[strlen(scan_format)], 
				proc_extended?((proc_extended-1)?"cpu%d %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld\n":"cpu%d %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld %%*ld\n"):"cpu%d %%*ld %%*ld %%*ld %%*ld\n",
				i);
		}
		sprintf(&scan_format[strlen(scan_format)], 
			proc_extended?((proc_extended-1)?"cpu%d %%ld %%ld %%ld %%ld %%*ld %%*ld %%*ld %%*ld\n":"cpu%d %%ld %%ld %%ld %%ld %%*ld %%*ld %%*ld\n"):"cpu%d %%ld %%ld %%ld %%ld\n",
			state.cpu_number);
	}
#else
	if ( state.cpu_number < 0 ) {
		sprintf(scan_format, "cpu %%ld %%ld %%ld %%ld\n");
	} else {
		sprintf(scan_format, "cpu %%*ld %%*ld %%*ld %%*ld\n");
		for (i=0; i<state.cpu_number; ++i) {
			sprintf(&scan_format[strlen(scan_format)], 
					"cpu%d %%*ld %%*ld %%*ld %%*ld\n",
					i);
		}
		sprintf(&scan_format[strlen(scan_format)], 
				"cpu%d %%ld %%ld %%ld %%ld\n",
				state.cpu_number);
	}
#endif
#ifdef DEBUG
	printf("The format is [%s]\n", scan_format);
#endif
}

/*
 * reads the /proc/stat
 */
void read_stat()
{
	int fd, len;

#ifdef __hpux__
	struct pst_dynamic store_pst_dynamic;
	long int *p;
	int i;
#endif

#ifdef _AIX32   /* AIX > 3.1 */
        time_t *p;
        struct sysinfo sysinfo;
#endif          

	/*
	 * Save the old values 
	 */
	memcpy(&last, &fresh, sizeof(last));

#ifdef __FreeBSD__
	if (state.cpu_number >= 0 && cp_times_mib[0] != 0) {
		long cp_times[MAX_CPU][CPUSTATES];
		size_t cp_times_len = sizeof(cp_times);
		int error = sysctl(cp_times_mib, 2, cp_times, &cp_times_len, NULL, 0);
		if (error) {
			printf("ascpu: cannot sysctl cp_times\n");
			exit(1);
		}

		long *cp_time = cp_times[state.cpu_number];
		fresh.load[0] = cp_time[CP_USER];
		fresh.load[1] = cp_time[CP_NICE];
		fresh.load[2] = cp_time[CP_SYS] + cp_time[CP_INTR];
		fresh.load[3] = cp_time[CP_IDLE];
	} else if (state.cpu_number == -1 && cp_time_mib[0] != 0) {
		long cp_time[CPUSTATES];
		size_t cp_time_len = sizeof(cp_time);
		int error = sysctl(cp_time_mib, 2, cp_time, &cp_time_len, NULL, 0);
		if (error) {
			printf("ascpu: cannot sysctl cp_time\n");
			exit(1);
		}

		fresh.load[0] = cp_time[CP_USER];
		fresh.load[1] = cp_time[CP_NICE];
		fresh.load[2] = cp_time[CP_SYS] + cp_time[CP_INTR];
		fresh.load[3] = cp_time[CP_IDLE];
	} else {
		if (nlst[0].n_type == 0) {
			printf("ascpu : cannot get nlist\n");
			exit(1);
		}
		if (kvm_read(kd, nlst[0].n_value, &fresh, sizeof(fresh)) != sizeof(fresh)) {
			printf("ascpu : cannot read kvm\n");
			exit(1);
		}
		/* compatible with Linux(overwrite 'interrupt' with 'idle' field) */
		fresh.load[3] = fresh.load[4];
	}
#endif

#ifdef __hpux__

	/*
        params: structure buf pointer, structure size, num of structs
        some index. HP say always 1, 0 for the last two. the order in
        fresh[] matches HP's psd_cpu_time[] and psd_mp_cpu_time[N][],
        so I can use the indices in dk.h for everything
	*/

	if( pstat_getdynamic( &store_pst_dynamic, \
			      sizeof store_pst_dynamic, 1, 0 )) {

	  if( state.cpu_number < 0 ) {
	    /* give the average over all processors */
	    p = store_pst_dynamic.psd_cpu_time;
	  }
	  else {
	    /* user-specified processor only */
	    p = store_pst_dynamic.psd_mp_cpu_time[state.cpu_number];
	  }

	  fresh.load[CP_USER] = p[CP_USER];
	  fresh.load[CP_NICE] = p[CP_NICE];
	  fresh.load[CP_SYS]  = p[CP_SYS] + p[CP_BLOCK] +\
	    p[CP_INTR] + p[CP_SSYS] + p[CP_SWAIT];
	  fresh.load[CP_IDLE] = p[CP_IDLE] + p[CP_WAIT];

	}
	else { 
	  printf("ascpu: Error while calling pstat_getdynamic(2)\n");
	  ascpu_cleanup();
	}

#endif		

#ifdef _AIX32   /* AIX > 3.1 */
        if (lseek (kmem, sysinfo_offset, SEEK_SET) != sysinfo_offset) {
          printf("ascpu : cannot lseek %s\n", KMEM_FILE);
          ascpu_cleanup();
        }
        if (read (kmem, (char *)&sysinfo, sizeof(struct sysinfo))
            != sizeof(struct sysinfo)) {
          printf("ascpu : cannot read %s\n", KMEM_FILE);
          ascpu_cleanup();
        }
        p = sysinfo.cpu;
        fresh.load[0] = p[CPU_USER];
        fresh.load[1] = p[CPU_WAIT];
        fresh.load[2] = p[CPU_KERNEL];
        fresh.load[3] = fresh.load[4] = p[CPU_IDLE];
#endif /* _AIX32 */
				    
#ifdef __linux__

	if ((fd = open(state.proc_stat_filename, O_RDONLY)) == -1) {
		printf("ascpu : cannot open %s\n", state.proc_stat_filename);
		exit(1);
	}
	len = read(fd, buffer, sizeof(buffer)-1);
	close(fd);
	if (len == 0) return; /* EOF? */
	if (len < 0) {
		printf("ascpu : cannot read %s\n", state.proc_stat_filename);
		exit(1);
	}
	buffer[len] = '\0';

	len = sscanf(buffer, scan_format,
		&fresh.load[0],
		&fresh.load[1],
		&fresh.load[2],
		&fresh.load[3]);
	if ( len <= 0 ) {
		printf("ascpu : invalid character while reading %s\n", 
			state.proc_stat_filename); 
	}
#endif
	/*
	 * Check if need "nice" CPU time at all
	 */
	if ( state.no_nice ) {
		fresh.load[3] += fresh.load[1];
		fresh.load[1] = 0;
	}
#ifdef DEBUG
	printf("diff:		%ld	%ld	%ld	%ld	%ld\n", 
		fresh.load[0] - last.load[0],
		fresh.load[1] - last.load[1],
		fresh.load[2] - last.load[2],
		fresh.load[3] - last.load[3], 
		fresh.load[4] - last.load[4] );
#endif
}


#ifdef FOR_CLICK
/*
 * This function executes an external command while 
 * checking whether we should drop the privileges.
 *
 * Since we might need privileges later we fork and
 * then drop privileges in one of the instances which
 * will then execute the command and die.
 *
 * This fixes the security hole for FreeBSD and AIX
 * where this program needs privileges to access
 * the system information.
 */
void ExecuteExternal()
{
	uid_t ruid, euid;
	int pid;
#ifdef DEBUG
	printf("ascpu: system(%s)\n",Command);
#endif

	if (setgid(getgid()) != 0)
		err(1, "Can't drop setgid privileges");
	if (setuid(getuid()) != 0)
		err(1, "Can't drop setuid privileges");

	if( ! Command ) {
		return;
	}
	ruid = getuid();
	euid = geteuid();
	if ( ruid == euid ) {
		system( Command );
		return;
	}
	pid = fork();
	if ( pid == -1 ) {
		printf("ascpu : fork() failed (%s), command not executed", 
				strerror(errno));
		return;
	}
	if ( pid != 0 ) {
		/* parent process simply waits for the child and continues */
		if ( waitpid(pid, 0, 0) == -1 ) {
			printf("ascpu : waitpid() for child failed (%s)", 
				strerror(errno));
		}
		return;
	}
	/* 
	 * child process drops the privileges
	 * executes the command and dies
	 */
	if ( setuid(ruid) ) {
		printf("ascpu : setuid failed (%s), command not executed",
				strerror(errno));
		exit(127);
	}
	system( Command );
	exit(0);
}
#endif

/* 
 * This checks for X11 events. We distinguish the following:
#ifdef FOR_CLICK
 * - request to execute some command
#endif
 * - request to repaint the window
 * - request to quit (Close button)
 */
void CheckX11Events()
{
        XEvent Event;
        while (XPending(mainDisplay)) { 
                XNextEvent(mainDisplay, &Event);
                switch(Event.type)
                {
#ifdef FOR_CLICK
		case ButtonPress:
			ExecuteExternal();
			break;
#endif
                case Expose:
                        if(Event.xexpose.count == 0) {
				++update_request;
			}
                        break;
                case ClientMessage:
                        if ((Event.xclient.message_type == wm_protocols)
                          && (Event.xclient.data.l[0] == wm_delete_window))
			{
#ifdef DEBUG
				printf("caught wm_delete_window, closing\n");
#endif
                                ascpu_cleanup();
			}
                        break;
                }
        }
}

void ascpu_redraw()
{
	draw_window(mainWindow);
	draw_window(iconWindow);
	update_request = 0;
}

void ascpu_update()
{
	time_t cur_time;
	cur_time = time(0);
	if ( abs(cur_time - last_time) >= state.update_interval ) {
		last_time = cur_time;
		read_stat();
		update_running();
		update_average();
	}
	CheckX11Events();
	if ( update_request ) {
		ascpu_redraw();
	}
}

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
			char * idle_color)
{
	int screen;
	Status status;
	XWindowAttributes winAttr;
	XSizeHints SizeHints;
	XTextProperty title;
	XClassHint classHint;
	int gravity;
	XWMHints WmHints;
	XEvent Event;
	int color_depth;
	int tmp;
	int result;
	int x_negative = 0;
	int y_negative = 0;

	mainDisplay = XOpenDisplay(display_name);
	if ( ! mainDisplay ) {
		printf("ascpu : grrrr... can't open display %s. Sorry ...\n",
			XDisplayName(display_name));
		exit(1);
	}
	screen = DefaultScreen(mainDisplay);
	Root = RootWindow(mainDisplay, screen);
	back_pix = GetColor(state.bgcolor, mainDisplay, Root);
	fore_pix = GetColor(state.fgcolor, mainDisplay, Root);
	color_depth = DefaultDepth(mainDisplay, screen);
#ifdef DEBUG    
        printf("ascpu : detected color depth %d bpp, using %d bpp\n",
                color_depth, color_depth);
#endif          
	/* adjust the background pixmap */
	if (pushed_in) {
		sprintf(bgpixmap_color[0],
			". c %s",
			DarkenCharColor(state.bgcolor, 1.6, mainDisplay, Root));
		sprintf(bgpixmap_color[1],
			"c c %s",
			state.bgcolor);
		sprintf(bgpixmap_color[2],
			"q c %s",
			LightenCharColor(state.bgcolor, 2.8, mainDisplay, Root));
	} else {
		sprintf(bgpixmap_color[2],
			"q c %s",
			DarkenCharColor(state.bgcolor, 1.2, mainDisplay, Root));
		sprintf(bgpixmap_color[1],
			"c c %s",
			state.bgcolor);
		sprintf(bgpixmap_color[0],
			". c %s",
			LightenCharColor(state.bgcolor, 2.5, mainDisplay, Root));
	}
	for (tmp = 0; tmp < 3; ++tmp)
		background[tmp+1] = bgpixmap_color[tmp];

	backgroundXpm.attributes.valuemask |=
                (XpmReturnPixels | XpmReturnExtensions);
        status = XpmCreatePixmapFromData(
                mainDisplay,                    /* display */
                Root,                           /* window */
                background,                     /* xpm */ 
                &backgroundXpm.pixmap,          /* resulting pixmap */
                &backgroundXpm.mask,
                &backgroundXpm.attributes
                );
        if(status != XpmSuccess) {
                printf("ascpu : (%d) not enough free color cells for background.\n", status);   
		ascpu_cleanup();
        }       
#ifdef DEBUG
	printf("bg pixmap %d x %d\n", 
		backgroundXpm.attributes.width,
		backgroundXpm.attributes.height);
#endif
	if (strlen(mainGeometry)) {
		/* Check the user-specified size */
		result = XParseGeometry(
			mainGeometry,
			&SizeHints.x,
			&SizeHints.y,
			&SizeHints.width,
			&SizeHints.height
			);
		if (result & XNegative) 
			x_negative = 1;
		if (result & YNegative) 
			y_negative = 1;
	}

        SizeHints.flags= USSize|USPosition;
        SizeHints.x = 0; 
        SizeHints.y = 0; 
        XWMGeometry(
                mainDisplay,
                screen,
                mainGeometry,
                NULL,
                1,
                & SizeHints,
                &SizeHints.x,
                &SizeHints.y,
                &SizeHints.width,
                &SizeHints.height,
                &gravity
                );
        SizeHints.min_width =
        SizeHints.max_width =
        SizeHints.width = backgroundXpm.attributes.width;
        SizeHints.min_height =
        SizeHints.max_height =
        SizeHints.height= backgroundXpm.attributes.height;
        SizeHints.flags |= PMinSize|PMaxSize;

	/* Correct the offsets if the X/Y are negative */
	SizeHints.win_gravity = NorthWestGravity;
	if (x_negative) {
		SizeHints.x -= SizeHints.width;
		SizeHints.win_gravity = NorthEastGravity;
	}
	if (y_negative) {
		SizeHints.y -= SizeHints.height;
		if (x_negative)
			SizeHints.win_gravity = SouthEastGravity;
		else
			SizeHints.win_gravity = SouthWestGravity;
	}
	SizeHints.flags |= PWinGravity;

        mainWindow = XCreateSimpleWindow(
                mainDisplay,            /* display */
                Root,                   /* parent */
                SizeHints.x,            /* x */
                SizeHints.y,            /* y */
                SizeHints.width,        /* width */
                SizeHints.height,       /* height */
                0,                      /* border_width */
                fore_pix,               /* border */
                back_pix                /* background */
                );
                
        iconWindow = XCreateSimpleWindow(
                mainDisplay,            /* display */
                Root,                   /* parent */
                SizeHints.x,            /* x */
                SizeHints.y,            /* y */
                SizeHints.width,        /* width */
                SizeHints.height,       /* height */
                0,                      /* border_width */
                fore_pix,               /* border */
                back_pix                /* background */
                );
                
        XSetWMNormalHints(mainDisplay, mainWindow, &SizeHints);
        status = XClearWindow(mainDisplay, mainWindow);
        
        status = XGetWindowAttributes(
                mainDisplay,    /* display */
                mainWindow,     /* window */
                & winAttr       /* window_attributes_return */
                );
        status = XSetWindowBackgroundPixmap(
                mainDisplay,            /* display */
                mainWindow,             /* window */
                backgroundXpm.pixmap    /* background_pixmap */
                );
        status = XSetWindowBackgroundPixmap(
                mainDisplay,            /* display */
                iconWindow,             /* window */
                backgroundXpm.pixmap    /* background_pixmap */
                );
                
        status = XStringListToTextProperty(&window_name, 1, &title);
        XSetWMName(mainDisplay, mainWindow, &title);
        XSetWMName(mainDisplay, iconWindow, &title);
        
        classHint.res_name = "ascpu" ;
        classHint.res_class = "ASCPU";
        XSetClassHint(mainDisplay, mainWindow, &classHint);
        XStoreName(mainDisplay, mainWindow, window_name);
        XSetIconName(mainDisplay, mainWindow, window_name);

        status = XSelectInput(
                mainDisplay,    /* display */
                mainWindow,     /* window */
                ExposureMask    /* event_mask */
#ifdef FOR_CLICK
		| ButtonPressMask
#endif
                );
                
        status = XSelectInput(
                mainDisplay,    /* display */
                iconWindow,     /* window */
                ExposureMask    /* event_mask */
#ifdef FOR_CLICK
		| ButtonPressMask
#endif
                );
                
        /* Creating Graphics context */
        mainGCV.foreground = fore_pix;
        mainGCV.background = back_pix;
        mainGCV.graphics_exposures = False;
        mainGCV.line_style = LineSolid;
        mainGCV.fill_style = FillSolid;
        mainGCV.line_width = 1;
        mainGC = XCreateGC(
                mainDisplay,
                mainWindow, 
                GCForeground|GCBackground|GCLineWidth|
                        GCLineStyle|GCFillStyle,
                &mainGCV
                );
                
        status = XSetCommand(mainDisplay, mainWindow, argv, argc);
        
        /* Set up the event for quitting the window */
        wm_delete_window = XInternAtom(
                mainDisplay, 
                "WM_DELETE_WINDOW",     /* atom_name */
                False                   /* only_if_exists */
                );
        wm_protocols = XInternAtom(
                mainDisplay,
                "WM_PROTOCOLS",         /* atom_name */
                False                   /* only_if_exists */
                );
        status = XSetWMProtocols(
                mainDisplay, 
                mainWindow,
                &wm_delete_window,
                1
                );
        status = XSetWMProtocols(
                mainDisplay, 
                iconWindow,
                &wm_delete_window,
                1
                );
                
        WmHints.flags = StateHint | IconWindowHint;
        WmHints.initial_state = 
                withdrawn ? WithdrawnState :
                        iconic ? IconicState : NormalState;
        WmHints.icon_window = iconWindow;
        if ( withdrawn ) {
                WmHints.window_group = mainWindow;
                WmHints.flags |= WindowGroupHint;
        }       
        if ( iconic || withdrawn ) {
                WmHints.icon_x = SizeHints.x;
                WmHints.icon_y = SizeHints.y;
                WmHints.flags |= IconPositionHint;
        }       
        XSetWMHints(
                mainDisplay,
                mainWindow, 
                &WmHints);
                
        /* Finally show the window */
        status = XMapWindow(
                mainDisplay,
                mainWindow
                );
                
	/* Get colors while waiting for Expose */
	if (idle_color_defined)
		pix[0] = GetColor(idle_color, mainDisplay, Root);
	else
		pix[0] = GetColor(state.bgcolor, mainDisplay, Root);
	if (sys_color_defined)
		pix[1] = GetColor(sys_color, mainDisplay, Root);
	else
		pix[1] = GetColor(state.fgcolor, mainDisplay, Root);
	if (nice_color_defined)
		pix[2] = GetColor(nice_color, mainDisplay, Root);
	else
		pix[2] = DarkenColor(state.fgcolor, 1.2, mainDisplay, Root);
	if (user_color_defined)
		pix[3] = GetColor(user_color, mainDisplay, Root);
	else
		pix[3] = DarkenColor(state.fgcolor, 1.4, mainDisplay, Root);

	/* 
	 * We have to "reset" the values of load by reading it in advance
	 */
	average_history = malloc(sizeof(struct cpu_load_struct) * 
			state.avg_samples);
	if (! average_history ) {
		printf("ascpu: failed to malloc average list (%ld bytes)\n",
			sizeof(struct cpu_load_struct) * state.avg_samples);
		ascpu_cleanup();
	}
#ifdef	__FreeBSD__
	size_t len = 2;
	sysctlnametomib("kern.cp_times", cp_times_mib, &len);
	len = 2;
	sysctlnametomib("kern.cp_time", cp_time_mib, &len);
	if ((kd = kvm_open(NULL, NULL, NULL, O_RDONLY, "kvm_open")) != NULL) {
		kvm_nlist(kd, nlst);
	}
#endif

#ifdef _AIX32   /* AIX > 3.1 */
        nlist ("/unix", namelist);
        if (namelist[0].n_value == 0) {
                printf("ascpu : cannot get nlist\n");
                exit(1);
        }
        sysinfo_offset = namelist[0].n_value;
        kmem = open (KMEM_FILE, O_RDONLY);
        if (kmem < 0) {
                printf("ascpu : cannot open %s\n", KMEM_FILE);
                exit(1);
        }
#endif /* _AIX32 */

	/*running_counter = state.hist_samples;*/
	prepare_format();
	read_stat();

        /* wait for the Expose event now */
        XNextEvent(mainDisplay, &Event); 
        /* We 've got Expose -> draw the parts of the window. */
        ascpu_redraw(); 
        XFlush(mainDisplay);
}

