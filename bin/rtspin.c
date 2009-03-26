#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>


#include "litmus.h"
#include "common.h"


static double wctime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec + 1E-6 * tv.tv_usec);
}

void usage(char *error) {
	fprintf(stderr, "Error: %s\n", error);
	fprintf(stderr,
		"Usage: rt_spin [-w] [-p PARTITION] [-c CLASS] WCET PERIOD DURATION\n"
		"       rt_spin -l\n");
	exit(1);
}

#define NUMS 4096
static int num[NUMS];
static double loop_length = 1.0;

static int loop_once(void)
{
	int i, j = 0;
	for (i = 0; i < NUMS; i++)
		j += num[i]++;
	return j;
}

static int loop_for(double exec_time)
{
	double t = 0;
	int tmp = 0;
	while (t + loop_length < exec_time) {
		tmp += loop_once();
		t += loop_length;
	}
	return tmp;
}

static void fine_tune(double interval)
{
	double start, end, delta;

	start = wctime();
	loop_for(interval);
	end = wctime();
	delta = (end - start - interval) / interval;
	if (delta != 0)
		loop_length = loop_length / (1 - delta);
}

static void configure_loop(void)
{
	double start;

	/* prime cache */
	loop_once();
	loop_once();
	loop_once();

	/* measure */
	start = wctime();
	loop_once(); /* hope we didn't get preempted  */
	loop_length = wctime();
	loop_length -= start;

	/* fine tune */
	fine_tune(0.1);
	fine_tune(0.1);
	fine_tune(0.1);
}

static void debug_delay_loop(void)
{
	double start, end, delay;
	printf("loop_length=%f\n", loop_length);
	while (1) {
		for (delay = 0.5; delay > 0.01; delay -= 0.01) {
			start = wctime();
			loop_for(delay);
			end = wctime();
			printf("%6.4fs: looped for %10.8fs, delta=%11.8fs, error=%7.4f%%\n",
			       delay,
			       end - start,
			       end - start - delay,
			       100 * (end - start - delay) / delay);
		}
	}
}

static int job(double exec_time)
{
	loop_for(exec_time);
	sleep_next_period();
	return 0;
}

#define OPTSTR "p:c:wl"

int main(int argc, char** argv) 
{
	int ret;
	lt_t wcet;
	lt_t period;
	int migrate = 0;
	int cpu = 0;
	int opt;
	int wait = 0;
	int test_loop = 0;
	double duration, start;
	task_class_t class = RT_CLASS_HARD;

	while ((opt = getopt(argc, argv, OPTSTR)) != -1) {
		switch (opt) {
		case 'w':
			wait = 1;
			break;
		case 'p':
			cpu = atoi(optarg);
			migrate = 1;
			break;
		case 'c':
			class = str2class(optarg);
			if (class == -1)
				usage("Unknown task class.");
			break;
		case 'l':
			test_loop = 1;
			break;
		case ':':
			usage("Argument missing.");
			break;
		case '?':
		default:
			usage("Bad argument.");
			break;
		}
	}


	configure_loop();

	if (test_loop) {
		debug_delay_loop();
		return 0;
	}

	if (argc - optind < 3)
		usage("Arguments missing.");       
	wcet     = atoi(argv[optind + 0]);
	period   = atoi(argv[optind + 1]);
	duration = atof(argv[optind + 2]);
	if (wcet <= 0)
		usage("The worst-case execution time must be a "
		      "positive number.");
	if (period <= 0)
		usage("The period must be a positive number.");
	if (wcet > period) {
		usage("The worst-case execution time must not "
		      "exceed the period.");
	}

	if (migrate) {
		ret = be_migrate_to(cpu);
		if (ret < 0)
			bail_out("could not migrate to target partition");
	}

	ret = sporadic_task(wcet, period, 0, cpu, class, migrate);
	
	if (ret < 0)
		bail_out("could not become rt tasks.");

	ret = init_litmus();
	if (ret < 0)
		perror("init_litmus()");

	ret = task_mode(LITMUS_RT_TASK);
	if (ret != 0)
		bail_out("task_mode()");

	if (wait) {
		ret = wait_for_ts_release();
		if (ret != 0)
			bail_out("wait_for_ts_release()");
	}


	start = wctime();

	while (start + duration > wctime()) {
		job(wcet * 0.0009); /* 90% wcet, in seconds */
	}

	return 0;
}
