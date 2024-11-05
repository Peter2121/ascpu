/*
 * ascpu is the CPU statistics monitor utility for X Windows
 * Copyright (c) 1998-2000  Albert Dorofeev <albert@tigr.net>
 * For the updates see http://www.tigr.net/
 *
 * This software is distributed under GPL. For details see LICENSE file.
 */

#ifndef _state_h_
#define _state_h_

/* file to read for stat info */
#define PROC_STAT "/proc/stat"

/* The maximum number of CPUs in the SMP system */
#ifdef __hpux__
#include <sys/pstat.h>
#define MAX_CPU PST_MAX_PROCS
#else
#define MAX_CPU 16
#endif

struct ascpu_state {
	long int update_interval; /* interval (sec) to check the statistics */
	long int hist_samples; /* num of samples to collect for the history */
	long int avg_samples; /* number of samples to collect for the average */
	int no_nice; /* do not show nice CPU time */
	int cpu_number; /* the number of the processor (-1 = total) */
	char proc_stat_filename[256];
	char bgcolor[50];
	char fgcolor[50];
};

#endif

