/****************************************************************************
   Program:     $Id: rtgplot.c,v 1.37 2003/10/02 15:27:52 rbeverly Exp $
   Author:      $Author: rbeverly $
   Date:        $Date: 2003/10/02 15:27:52 $
   Description: RTG traffic grapher
                Utilizes cgilib (http://www.infodrom.org/projects/cgilib/)
                for the CGI interface.
****************************************************************************/

/*
  We define the image as follows:

  +--------------------------------------------------------------+   ^
  |                                  BORDER_T                    |   |  
  |            +---------------------------------------------+   |   |  
  |            |                    ^                        |   |   | Y
  |            |                    |                        | B |   | I
  |            |                    |                        | O |   | M
  |            |                    |                        | R |   | G
  |            |                    |                        | D |   | _
  |<-BORDER_L->|               YPLOT_AREA                    | E |   | A
  |            |                    |                        | R |   | R
  |            |                    |                        | _ |   | E
  |            |                    |                        | R |   | A
  |            |                    |                        |   |   |
  |            |                    |                        |   |   |
  |            | <----------XPLOT_AREA---------------------> |   |   |
  |            +---------------------------------------------+   |   |
  |                                  BORDER_B                    |   |
  +--------------------------------------------------------------+   ^

  <-----------------------XIMG_AREA------------------------------>   

*/

/*
  REB - rtgplot in a nutshell:
	1. Grab command line or HTTP arguments
	2. Read RTG configuration rtg.conf
	3. Create empty graph with arrows, borders, etc
	4. Populate a data_t linked list for each interface
	5. Calculate a rate_t rate struct for each data linked list that
	   defines total, max, avg, current rates
	6. Normalize each line in relation to other lines, graph size, etc
	7. Plot each line and legend
	8. Plot X and Y scales
	9. Write out graph 

  There are a bunch of subtleties beyond this that we try to comment.
*/

#include "common.h"
#include "rtg.h"
#include "rtgplot.h"
#include "cgi.h"

/* dfp is a debug file pointer.  Points to stderr unless debug=level is set */
FILE *dfp = NULL;

int main(int argc, char **argv) {
	MYSQL           mysql;
	gdImagePtr      img;
	data_t         *data[MAXTABLES][MAXIIDS];
	data_t			*dataPtr = NULL;
	rate_t          rate[MAXTABLES][MAXIIDS];
	graph_t         graph;
	color_t        *colors = NULL;
    arguments_t     arguments;

	char            query[BUFSIZE];
	char			intname[BUFSIZE];
	int             i, j;
	char           *web = NULL;
	int             offset = 0;


	/* Check to see if we're being called as a CGI */
	web = getenv("SERVER_NAME");
	dfp = stderr;

	/* Check argument count */
	if (argc > 1 && argc < 5 && (!web))
		usage(argv[0]);

	bzero(&arguments, sizeof(arguments_t));
	bzero(&graph, sizeof(graph_t));
	config_defaults(&set);
	sizeDefaults(&graph);

	/* Called from the command line with arguments */
	if (argc > 1 && (!web)) 
		parseCmdLine(argc, argv, &arguments, &graph);
	/* Called via a CGI */
	else 
		parseWeb(&arguments, &graph);

	graph.range.dataBegin = graph.range.end;
	sizeImage(&graph);

	/* Read configuration file to establish local environment */
	if (arguments.conf_file) {
		if ((read_rtg_config(arguments.conf_file, &set)) < 0) {
			fprintf(dfp, "Could not read config file: %s\n", arguments.conf_file);
			exit(-1);
		}
	} else {
		arguments.conf_file = malloc(BUFSIZE);
		for (i = 0; i < CONFIG_PATHS; i++) {
			snprintf(arguments.conf_file, BUFSIZE, "%s%s", config_paths[i], DEFAULT_CONF_FILE);
			if (read_rtg_config(arguments.conf_file, &set) >= 0) {
				break;
			}
			if (i == CONFIG_PATHS - 1) {
				snprintf(arguments.conf_file, BUFSIZE, "%s%s", config_paths[0], DEFAULT_CONF_FILE);
				if ((write_rtg_config(arguments.conf_file, &set)) < 0) {
					fprintf(dfp, "Couldn't write config file.\n");
					exit(-1);
				}
			}
		}
	}

	/* Initialize array of pointers */
	for (i = 0; i < MAXTABLES; i++) {
		for (j = 0; j < MAXIIDS; j++) {
			data[i][j] = NULL;
		}
	}

	/* Attempt to connect to the MySQL Database */
	if (rtg_dbconnect(set.dbdb, &mysql) < 0) {
		fprintf(dfp, "** Database error - check configuration.\n");
		exit(-1);
	}

    /* Initialize the graph */
	create_graph(&img, &graph);
	init_colors(&img, &colors);
	draw_grid(&img, &graph);

	/* set xoffset to something random, but suitably large */
	graph.xoffset = 2000000000;

	/* If we're y-scaling the plot to max interface speed */
	if (graph.scaley) {
#ifdef HAVE_STRTOLL
		graph.ymax = (float) intSpeed(&mysql, arguments.iid[0]);
#else
		graph.ymax = (float) intSpeed(&mysql, arguments.iid[0]);
#endif
	}

	/* Populate the data linked lists and get graph stats */
	for (i = 0; i < arguments.tables_to_plot; i++) {
		for (j = 0; j < arguments.iids_to_plot; j++) {
			snprintf(query, sizeof(query), "SELECT counter, UNIX_TIMESTAMP(dtime) FROM %s WHERE dtime>FROM_UNIXTIME(%ld) AND dtime<=FROM_UNIXTIME(%ld) AND id=%d ORDER BY dtime", 
				arguments.table[i], graph.range.begin, graph.range.end, arguments.iid[j]);

			if (populate(query, &mysql, &data[i][j], &graph) < 0) {
				/* Recreate the query to get the last point in the DB.  
					Then recall populate. */
				snprintf(query, sizeof(query), "SELECT counter, UNIX_TIMESTAMP(dtime) FROM %s WHERE id=%d ORDER BY dtime DESC LIMIT 1",
					arguments.table[i], arguments.iid[j]);
					if (populate(query, &mysql, &data[i][j], &graph) < 0) {
						if (set.verbose >= DEBUG)
							fprintf(dfp, "  No data to populate() for table: %d int: %d\n", i, j);
					}
			}
			/* Our graph time range is the largest of the ranges;
			   set maximum X */
			if (graph.range.end - graph.range.dataBegin > graph.xmax)
				graph.xmax = graph.range.end - graph.range.dataBegin;
			if (graph.range.dataBegin < graph.xoffset)
				graph.xoffset = graph.range.dataBegin;

			/* we're plotting impulses or gauge */
			if (graph.impulses || graph.gauge) {
				calculate_total(&data[i][j], &rate[i][j], arguments.factor);
				if (!graph.scaley && (rate[i][j].max > graph.ymax))
					/* Extend Y-Axis to prevent line from
					   tracing top of YPLOT_AREA  */ 
                                        graph.ymax = rate[i][j].max * 1.05;
			} else {
				calculate_rate(&data[i][j], &rate[i][j], arguments.factor);
				/* maximum Y value is largest of all line rates */
				if (!graph.scaley && (rate[i][j].max > graph.ymax))
					graph.ymax = rate[i][j].max;
			}
		}
	}

	/* Plot each line and legend */
	for (i = 0; i < arguments.tables_to_plot; i++) {
		if (arguments.aggregate) {
			for (j = 0; j < arguments.iids_to_plot; j++) {
				if (data[i][j] && data[i][j]->next)
					normalize(data[i][j], &graph);
			}
			for (j = 1; j < arguments.iids_to_plot; j++) {
				dataAggr(data[i][0], data[i][j], &rate[i][0], &rate[i][j], &graph);
			}
			normalize(data[i][0], &graph);
			if (arguments.filled)
				plot_line(data[i][0], &img, &graph, colors->shade, TRUE);
			else
				plot_line(data[i][0], &img, &graph, colors->shade, FALSE);
			snprintf(intname, sizeof(intname), "%sAVG", arguments.table[i]);
			plot_legend(&img, rate[i][0], &graph, colors->shade, intname, offset);
			offset++;
			colors = colors->next;
		} else {
			for (j = 0; j < arguments.iids_to_plot; j++) {
				/* Need at least two data points to make a line */
//	XXX			if (data[i][j] && data[i][j]->next) {
				if (data[i][j]) {
					normalize(data[i][j], &graph);
					if (set.verbose >= DEBUG) {
						dump_data(data[i][j]);
					}
					if ( (i==0) && (j==0) && arguments.filled) 
						plot_line(data[i][j], &img, &graph, colors->shade, TRUE);
					else
						plot_line(data[i][j], &img, &graph, colors->shade, FALSE);
					snprintf(intname, sizeof(intname), "%s%d", arguments.table[i], arguments.iid[j]);
					plot_legend(&img, rate[i][j], &graph, colors->shade, intname, offset);
					offset++;
				}
				colors = colors->next;
			}
		}
	}

	/* Plot percentile lines if necessary (only for the first interface!) */
	if (arguments.percentile) {
		for (i = 0; i < arguments.tables_to_plot; i++) {
			data[i][0] = sort_data( data[i][0], FALSE, FALSE);
			dataPtr = return_Nth(data[i][0], count_data(data[i][0]), arguments.percentile);
			if (set.verbose >= DEBUG) {
				fprintf(dfp, "Sorted elements of table %d:\n", i); 
				dump_data(data[i][0]);
				fprintf(dfp, "95th rate: %2.3f y-coord: %d\n", dataPtr->rate, dataPtr->y);
			}
			plot_Nth(&img, &graph, dataPtr);
		}
	}

	/* Put finishing touches on graph and write it out */
	draw_border(&img, &graph);
	draw_arrow(&img, &graph);
	plot_scale(&img, &graph);
	plot_labels(&img, &graph);
	write_graph(&img, arguments.output_file);

	/* Disconnect from the MySQL Database, exit. */
	rtg_dbdisconnect(&mysql);
	if (dfp != stderr) fclose(dfp);
	exit(0);
}


void dump_data(data_t * head) {
	data_t         *entry = NULL;
	int             i = 0;

	fprintf(dfp, "Dumping data:\n");
	entry = head;
	while (entry != NULL) {
		fprintf(dfp, "  [%d] Count: %lld TS: %ld Rate: %2.3f X: %d Y: %d\n",
		       ++i, entry->counter, entry->timestamp, entry->rate,
		       entry->x, entry->y);
		entry = entry->next;
	}
}


/*
 * if we're doing impulses, we can't calculate a rate.  Instead, we just
 * populate and figure out the max values for plotting
 */
/* Snarf the MySQL data into a linked list of data_t's */
int populate(char *query, MYSQL * mysql, data_t ** data, graph_t * graph) {
	MYSQL_RES      *result;
	MYSQL_ROW       row;
	data_t         *new = NULL;
	data_t         *last = NULL;
	range_t			*range;


	if (set.verbose >= HIGH)
		fprintf(dfp, "Populating (%s).\n", __FUNCTION__);

	range = &(graph->range);
	if (set.verbose >= DEBUG) 
		fprintf(dfp, "  Query String: %s\n", query);

	if (mysql_query(mysql, query)) {
		fprintf(dfp, "  Query error.\n");
		return (-1);
	}
	if ((result = mysql_store_result(mysql)) == NULL) {
		fprintf(dfp, "  Retrieval error.\n");
		return (-1);
	} else if (set.verbose >= LOW) {
		fprintf(dfp, "  Retrieved %llu rows.\n", mysql_num_rows(result));
	}

	while ((row = mysql_fetch_row(result))) {
		if ((new = (data_t *) malloc(sizeof(data_t))) == NULL) 
			fprintf(dfp, "  Fatal malloc error in populate.\n");
		/* Seems atoll is not very portable, nor desirable */
		/* new->counter = atoll(row[0]); */
#ifdef HAVE_STRTOLL
		new->counter = strtoll(row[0], NULL, 0);
#else
		new->counter = strtol(row[0], NULL, 0);
#endif
		new->timestamp = atoi(row[1]);
		new->next = NULL;
		(range->datapoints)++;
		if (new->counter > range->counter_max)
			range->counter_max = new->counter;
		if (*data != NULL) {
			last->next = new;
			last = new;
		} else {
			 /* Realign the start time to be consistent with
			 * actual DB data.  Use dataBegin, reset only if less
			 * than last dataBegin and larger than requested
			 * begin. */
			if (range->scalex) {
				if ((new->timestamp > range->begin) &&
				    (new->timestamp <= range->dataBegin)) {
					range->dataBegin = new->timestamp;
				}
			} else {
				range->dataBegin = range->begin;
			}
			*data = new;
			last = new;
		}
	}

	/* no data, go home */
	if (*data == NULL) {
		if (set.verbose >= DEBUG)
			fprintf(dfp, "  No Data Points %ld in %ld Seconds.\n", 
				range->datapoints, range->end - range->begin);
		return (-1);
	} else {
		 /* realign the end time to be consistent with what's in the DB */
		if (range->scalex)
			range->end = new->timestamp;

		if (set.verbose >= DEBUG)
			fprintf(dfp, "  %ld Data Points in %ld Seconds.\n",
			  range->datapoints, range->end - range->dataBegin);
	}
	return (range->datapoints);
}


void calculate_total(data_t ** data, rate_t * rate, int factor) {
	data_t         *entry = NULL;
	int		num_samples = 0;
	float		ratetmp;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Calc total (%s).\n", __FUNCTION__);
	entry = *data;
	rate->total = 0;
	while (entry != NULL) {
		num_samples++;
		if (set.verbose >= DEBUG)
			fprintf(dfp, "  [TS: %ld][Counter: %lld][Factor: %d]\n", entry->timestamp,
			       entry->counter, factor);
		ratetmp = entry->counter * factor;
		rate->total += ratetmp;
		rate->cur = ratetmp;
		if (ratetmp > rate->max)
			rate->max = ratetmp;
		entry->rate = ratetmp;
		entry = entry->next;
	}
	rate->avg = rate->total / num_samples;
}


void calculate_rate(data_t ** data, rate_t * rate_stats, int factor) {
	data_t         *entry = NULL;
	float           rate = 0.0;
	float           last_rate = 0.0;
	int             sample_secs = 0;
	int             last_sample_secs = 0;
	int             num_rate_samples = 0;
	int             i;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Calc rate (%s).\n", __FUNCTION__);

	for (i = 0; i < MAXTABLES; i++)
		bzero(rate_stats, sizeof(*rate_stats));

	entry = *data;
	if (entry == NULL)
		return;

	while (entry != NULL) {
		rate_stats->total += entry->counter;
		sample_secs = entry->timestamp;
		if (last_sample_secs != 0) {
			num_rate_samples++;
			rate = (float)entry->counter * factor / (sample_secs - last_sample_secs);
			/*
			 * Compensate for polling slop.
			 * set.highskewslop and lowskewslop are configurable.
			 * If two values are too far or too close together,
			 * in time, then set rate to last_rate.
			 */
			if (sample_secs - last_sample_secs > set.highskewslop * set.interval) {
				if (set.verbose >= LOW) {
					fprintf(dfp, "***Poll skew [elapsed secs=%d] [interval = %d] [slop = %2.2f]\n",
					       sample_secs - last_sample_secs, set.interval, set.highskewslop);
				}
				rate = last_rate;
			}
			if (sample_secs - last_sample_secs < set.lowskewslop * set.interval) {
				if (set.verbose >= LOW) {
					fprintf(dfp, "***Poll skew [elapsed secs=%d] [interval = %d] [slop = %2.2f]\n",
					       sample_secs - last_sample_secs, set.interval, set.lowskewslop);
				}
				rate = last_rate;
			}
			if (set.verbose >= DEBUG)
				fprintf(dfp, "  [row0=%lld][row1=%d][elapsed secs=%d][rate=%2.3f bps]\n", entry->counter, sample_secs, sample_secs - last_sample_secs, rate);
			if (rate < 0 && set.verbose >= LOW)
				fprintf(dfp, "  ***Err: Negative rate!\n");
			rate_stats->avg += rate;
			if (rate > rate_stats->max)
				rate_stats->max = rate;
		}
		entry->rate = rate;
		last_sample_secs = sample_secs;
		last_rate = rate;
		entry = entry->next;
	}
	rate_stats->cur = rate;
	rate_stats->avg = rate_stats->avg / num_rate_samples;

	/*
	 * A rate is calcuated as a function of time which requires at least
	 * two data points.  Because of this our first data point will always
	 * be zero, so set the first data point the same as the second
	 * datapoint
	 */
	if (num_rate_samples > 1) {
		(*data)->rate = (*data)->next->rate;
	}
}


/* REB - So here's the strategy for implementing aggregates.  We first
 do the normal populate and calculate_rate to get the data_t's for
 each interface in order.  Normalize all of the data_t's to see 
 where their X-axis points would be if we plotted them normally.
 We then make the first data_t of the table i.e. data_t[table][0] the 
 aggregate data list.  dataAggr() takes the master and what we want
 to add to it and finds coincidental X-axis points.  It sums the 
 rates of all interfaces into this master data_t.  We then renormalize
 the master data_t and plot it out. 

 Sorry to be long winded, but I'll never remember what I did if I 
 don't write it down.
*/
void dataAggr(data_t *aggr, data_t *head, rate_t *aggr_rate, rate_t *rate, graph_t *graph) {
	float last_rate;

    if (set.verbose >= HIGH)
        fprintf(dfp, "Aggregate (%s).\n", __FUNCTION__);

	last_rate = aggr->rate;
    while (head && aggr) {
        if (head->x < aggr->x) {
			if (set.verbose >= DEBUG) fprintf(dfp, ".h");
			head = head->next;
		}
        if (head->x > aggr->x) {
			if (set.verbose >= DEBUG) fprintf(dfp, ".a");
			aggr->rate = last_rate;
			aggr = aggr->next;
		}
        if (head->x == aggr->x) {
			if (set.verbose >= DEBUG) fprintf(dfp, ".+");
			aggr->rate = aggr->rate + head->rate;
			last_rate = aggr->rate;
			if (aggr->rate > graph->ymax) {
				graph->ymax = aggr->rate;
			}
			if (aggr->rate > aggr_rate->max) {
				aggr_rate->max = aggr->rate;
			}
			aggr_rate->cur = aggr->rate;
            head = head->next;
            aggr = aggr->next;
        }
    }
	(aggr_rate->avg)+= rate->avg;
	(aggr_rate->total)+= rate->total; 
	return;
}


void normalize(data_t *head, graph_t *graph) {
	data_t         *entry = NULL;
	long            time_offset = 0;
	float           pixels_per_sec = 0.0;
	float           pixels_per_bit = 0.0;
	int             last_x_pixel = 0;
	int             x_pixel = 0;
	int             y_pixel = 0;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Normalize (%s).\n", __FUNCTION__);

	if (graph->ymax > 0)
		pixels_per_bit = (float)graph->image.yplot_area / graph->ymax;
	if (graph->xmax > 0)
		pixels_per_sec = (float)graph->image.xplot_area / graph->xmax;

	if (graph->xmax > 0 && graph->xmax <= HOUR)
		graph->xunits = HOUR;
	else if (graph->xmax > HOUR && graph->xmax <= HOUR * DAY)
		graph->xunits = DAY;
	else if (graph->xmax > HOUR * DAY && graph->xmax <= HOUR * DAY * WEEK)
		graph->xunits = WEEK;
	else if (graph->xmax > HOUR * DAY * WEEK)
		graph->xunits = MONTH;

	if (graph->ymax > 0 && graph->ymax <= KILO)
		graph->yunits = 1;
	else if (graph->ymax > KILO && graph->ymax <= MEGA)
		graph->yunits = KILO;
	else if (graph->ymax > MEGA && graph->ymax <= GIGA)
		graph->yunits = MEGA;
	else if (graph->ymax > GIGA)
		graph->yunits = GIGA;
	else
		graph->yunits = -1;

	entry = head;
	time_offset = graph->range.dataBegin;
	if (set.verbose >= DEBUG) {
		fprintf(dfp, "  X-Max = %d Y-Max = %2.3f\n", graph->xmax, graph->ymax);
		fprintf(dfp, "  X-units = %d Y-units = %lld\n", graph->xunits, graph->yunits);
		fprintf(dfp, "  XPixels/Sec = %2.6f\n", pixels_per_sec);
		fprintf(dfp, "  YPixels/Bit = %2.10f\n", pixels_per_bit);
		fprintf(dfp, "  Timeoffset = %ld\n", time_offset);
	}
	while (entry != NULL) {
		x_pixel = (int)((entry->timestamp - time_offset) * pixels_per_sec + .5);
		y_pixel = (int)((entry->rate * pixels_per_bit) + .5);
		entry->x = x_pixel;
		entry->y = y_pixel;
		if (x_pixel != last_x_pixel) {
			if (set.verbose >= DEBUG) {
				fprintf(dfp, "  TS=%ld T=%ld X=%d R=%2.3f RN=%2.5f Y=%d\n",
				       entry->timestamp, entry->timestamp - time_offset, x_pixel,
				       entry->rate, entry->rate / graph->yunits, y_pixel);
			}
		}
		last_x_pixel = x_pixel;
		entry = entry->next;
	}

	return;
}



void usage(char *prog) {
	printf("Usage: %s -t tbls -i iids [-o file] [-u units] [-f factor] [-vv] [-mnb] [-d per] [-lgxy] begin end\n", prog);
	printf("    -o output file, PNG format (default to stdout)\n");
	printf("    -t DB table (can use multiple times)\n");
	printf("    -i interface id (can use multiple times)\n");
	printf("    -f to specify integer factor (e.g. bytes to bits)\n");
	printf("    -u y-axis units string\n");
	printf("    -p for impulses\n");
	printf("    -g for gauges\n");
	printf("    -l to fill area beneath first plot line\n");
	printf("    -a aggregate interface ids\n");
	printf("    -d to add dth percentile line\n");
	printf("    -x to scale xaxis to current time\n");
	printf("    -y to scale yaxis to interface speed\n");
	printf("    -v verbosity (can use multiple times)\n");
	printf("    -m,n,b set plot size to MxN with bottom border B pixels\n");
	printf("    begin and end are unix timestamps\n");
	exit(-1);
}


void dump_rate_stats(rate_t * rate) {
	fprintf(dfp, "[total = %lld][max = %2.3f][avg = %2.3f][cur = %2.3f]\n",
	       rate->total, rate->max, rate->avg, rate->cur);
}


char *units(float val, char *string) {
	if (val > TERA)
		snprintf(string, BUFSIZE, "%2.1f T", val / TERA);
	else if (val > GIGA)
		snprintf(string, BUFSIZE, "%2.1f G", val / GIGA);
	else if (val > MEGA)
		snprintf(string, BUFSIZE, "%2.1f M", val / MEGA);
	else if (val > KILO)
		snprintf(string, BUFSIZE, "%2.1f K", val / KILO);
	else
		snprintf(string, BUFSIZE, "%2.2f", val);
	return (string);
}


void plot_legend(gdImagePtr * img, rate_t rate, graph_t * graph, int color, char *interface, int offset) {
	char            total[BUFSIZE];
	char            max[BUFSIZE];
	char            avg[BUFSIZE];
	char            cur[BUFSIZE];
	char            string[BUFSIZE];
	int             i;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Plotting legend (%s).\n", __FUNCTION__);

	gdImageFilledRectangle(*img, BORDER_L,
			     BORDER_T + graph->image.yplot_area + 37 + 10 * offset,
	BORDER_L + 7, BORDER_T + graph->image.yplot_area + 44 + 10 * offset, color);
	gdImageRectangle(*img, BORDER_L, BORDER_T + graph->image.yplot_area + 37 + 10 * offset,
	       BORDER_L + 7, BORDER_T + graph->image.yplot_area + 44 + 10 * offset,
			 std_colors[black]);

	if (strlen(interface) > 17) {
		interface[17] = '\0';
	}
	snprintf(string, sizeof(string), "%s", interface);
	for (i = 0; i < (17 - strlen(interface)); i++) {
		snprintf(string, sizeof(string), "%s ", string);
	}
	if (graph->gauge) {
		snprintf(string, sizeof(string), "%s Max: %7s%s Avg: %7s%s Cur: %7s%s",
			string,
			units((float)rate.max, max), graph->units,
			units((float)rate.avg, avg), graph->units,
			units((float)rate.cur, cur), graph->units);
	}
	else if (graph->impulses) {
		snprintf(string, sizeof(string), "%s Total: %lld Max: %02.1f",
			interface, rate.total, (float)rate.max);
	}
	else {
		snprintf(string, sizeof(string), "%s Max: %7s%s Avg: %7s%s Cur: %7s%s [%7s]",
			string,
			units(rate.max, max), graph->units,
			units(rate.avg, avg), graph->units,
			units(rate.cur, cur), graph->units,
			units((float)rate.total, total));
	}
	gdImageString(*img, gdFontSmall, BORDER_L + 10,
		      BORDER_T + graph->image.yplot_area + 33 + (10 * offset), string, std_colors[black]);
}


void plot_line(data_t * head, gdImagePtr * img, graph_t * graph, int color, int filled) {
	data_t         *entry = NULL;
	float			pixels_per_sec = 0.0;
	time_t			now;
	int             lastx = 0;
	int             lasty = 0;
	int             skip = 0;
	int				datapoints = 0;
	int				xplot_area = 0;
	int				now_pixel = 0;
	gdPoint			points[4];

	entry = head;
	lasty = entry->y;
	datapoints = graph->range.datapoints;
	xplot_area = graph->image.xplot_area;
	/* The skip integer allows data points to be skipped when the scale
	   of the graph is such that data points overlap */
	skip = (int)datapoints / xplot_area;
	
	/* This nonsense is so that we don't have to depend on ceil/floor functions in math.h */
	if (datapoints % xplot_area != 0)
		skip++;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Plotting line.  Skip = %d (%s).\n", skip, __FUNCTION__);

	while (entry != NULL) {
		if (entry->x != (lastx + skip + 2)) {
			if (entry->y != 0 || lasty != 0) {
				if (graph->impulses) {
					gdImageLine(*img, entry->x + BORDER_L, graph->image.yimg_area - graph->image.border_b,
						    entry->x + BORDER_L, graph->image.yimg_area - graph->image.border_b - entry->y, color);
				} else {
					if (set.verbose >= DEBUG) {
						fprintf(dfp, "  Plotting from (%d,%d) to (%d,%d) ",
						       lastx + BORDER_L, graph->image.yimg_area - graph->image.border_b - lasty,
						       entry->x + BORDER_L, graph->image.yimg_area - graph->image.border_b - entry->y);
						fprintf(dfp, "[entry->x/y = %d/%d] [lastx/y = %d/%d]\n",
						       entry->x, entry->y, lastx, lasty);
					}
					/* REB - broken, fix someday if (filled) gdImageFilledRectangle(*img) */
					if (filled) {
						points[0].x = lastx + BORDER_L;
						points[0].y = graph->image.yimg_area - graph->image.border_b;
						points[1].x = lastx + BORDER_L;
						points[1].y = graph->image.yimg_area - graph->image.border_b - lasty;
						points[2].x = entry->x + BORDER_L;
						points[2].y = graph->image.yimg_area - graph->image.border_b - entry->y;
						points[3].x = entry->x + BORDER_L;
						points[3].y = graph->image.yimg_area - graph->image.border_b;
						gdImageFilledPolygon(*img, points, 4, color);
					} else {
						gdImageLine(*img, lastx + BORDER_L,
						    graph->image.yimg_area - graph->image.border_b - lasty, entry->x + BORDER_L,
						    graph->image.yimg_area - graph->image.border_b - entry->y, color);
					}
				}
				lasty = entry->y;
			}
			lastx = entry->x;
		}		/* if not skipping */
		entry = entry->next;
	}			/* while */

	/* If we're not aligning the time x-axis to the values in the
			database, make sure we're current up to time(now) */
	pixels_per_sec = (float)graph->image.xplot_area / graph->xmax;

	/* Find the x-pixel of the current time */
	time(&now);
	now_pixel = (int)((now - graph->range.dataBegin) * pixels_per_sec + .5);

	/* Only extend plot if now is not in plot future */
	if (now_pixel < BORDER_L + graph->image.xplot_area) {
		if (set.verbose >= HIGH) 
			fprintf(dfp, "Extending plot to current time %ld (x-pixel %d)\n", now, now_pixel);
		gdImageLine(*img, lastx + BORDER_L,
			graph->image.yimg_area - graph->image.border_b - lasty, now_pixel + BORDER_L,
			graph->image.yimg_area - graph->image.border_b - lasty, color);
	} else {
		if (set.verbose >= HIGH) 
			fprintf(dfp, "Current time %ld (x-pixel %d) extends past plot end.\n", now, now_pixel);
	}

	return;
}


/* Plot a Nth percentile line across the graph at the element nth points to */
void plot_Nth(gdImagePtr *img, graph_t *graph, data_t *nth) {
	int red;
	int xplot_area;
	int yplot_area;

	red = gdImageColorAllocate(*img, 255, 0, 0);
	xplot_area = graph->image.xplot_area;
	yplot_area = graph->image.yplot_area;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Plotting Nth Percentile (%s).\n", __FUNCTION__);
		gdImageLine(*img, BORDER_L,
			graph->image.yimg_area - graph->image.border_b - nth->y, 
			BORDER_L + graph->image.xplot_area,
			graph->image.yimg_area - graph->image.border_b - nth->y, 
			red);
	return;
}


void plot_scale(gdImagePtr *img, graph_t *graph) {
	struct tm      *thetime;
	float           pixels_per_sec = 0.0;
	float           pixels_per_bit = 0.0;
	char            string[BUFSIZE];
	int             i;
	int             last_day = 0;
	int             last_month = 0;
	long            seconds;
	float           rate = 0.0;
	float           plot_rate = 0.0;
	int             days = 0;
	int             skip = 0;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Plotting scale (%s).\n", __FUNCTION__);
	pixels_per_sec = (float)graph->image.xplot_area / graph->xmax;
	pixels_per_bit = (float)graph->image.yplot_area / graph->ymax;
	days = (int)graph->xmax / (14 * 60 * 60 * 24);
	/* need to skip days days if range is large */
	if (days < 1) {
		/* Draw X-axis time labels */
		for (i = 0; i <= graph->image.xplot_area; i += (graph->image.xplot_area / XTICKS)) {
			seconds = (long)i *(1 / pixels_per_sec);
			seconds += graph->xoffset;
			thetime = localtime(&seconds);
			snprintf(string, sizeof(string), "%d:%02d", thetime->tm_hour, thetime->tm_min);
			gdImageString(*img, gdFontSmall, i + BORDER_L - 15,
				      BORDER_T + graph->image.yplot_area + 5, string, std_colors[black]);
		}
	}
	/* Draw X-axis date labels */
	skip = (int)((graph->image.xplot_area / XTICKS) / (10 * (1 + days)));
	if (skip <= 0)
		skip = 1;
	for (i = 0; i <= graph->image.xplot_area; i += skip) {
		seconds = (long)i *(1 / pixels_per_sec);
		seconds += graph->xoffset;
		thetime = localtime(&seconds);
		if (thetime->tm_mon != last_month) {
			last_day = 0;
		}
		if ((thetime->tm_mday > last_day + days) || (i == 0)) {
			snprintf(string, sizeof(string), "%02d/%02d", thetime->tm_mon + 1, thetime->tm_mday);
			if (days < 1)
				gdImageString(*img, gdFontSmall, i + BORDER_L - 10, BORDER_T + graph->image.yplot_area + 16, string, std_colors[black]);
			else
				gdImageString(*img, gdFontSmall, i + BORDER_L - 10, BORDER_T + graph->image.yplot_area + 5, string, std_colors[black]);
			last_day = thetime->tm_mday;
			last_month = thetime->tm_mon;
		}
	}
	/* Draw Y-axis rate labels */
	for (i = 0; i <= graph->image.yplot_area; i += (graph->image.yplot_area / YTICKS)) {
		rate = (float)i *(1 / pixels_per_bit);
		plot_rate = rate / (graph->yunits);
		if (graph->ymax / graph->yunits < 10) {
			snprintf(string, sizeof(string), "%1.1f", plot_rate);
		} else if (graph->ymax / graph->yunits < 100) {
			snprintf(string, sizeof(string), "%2.0f", plot_rate);
		} else {
			snprintf(string, sizeof(string), "%3.0f", plot_rate);
		}
		if (rate > 0)
			gdImageString(*img, gdFontSmall, BORDER_L - 20, BORDER_T + graph->image.yplot_area - i - 5, string, std_colors[black]);
	}
}


void plot_labels(gdImagePtr * img, graph_t * graph) {
	char            string[BUFSIZE];
	float           title_offset = 0.0;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Plotting labels (%s).\n", __FUNCTION__);

	if (graph->yunits == KILO)
		snprintf(string, sizeof(string), "K%s", graph->units);
	else if (graph->yunits == MEGA)
		snprintf(string, sizeof(string), "M%s", graph->units);
	else if (graph->yunits == GIGA)
		snprintf(string, sizeof(string), "G%s", graph->units);
	else
		snprintf(string, sizeof(string), "%s", graph->units);
	gdImageStringUp(*img, gdFontSmall, BORDER_L - 40, BORDER_T + (graph->image.yplot_area * 2 / 3), string, std_colors[black]);

	title_offset = 1 - (0.01 * (strlen(VERSION) + strlen(COPYRIGHT) + 2));
	snprintf(string, sizeof(string), "%s %s", COPYRIGHT, VERSION);
	gdImageString(*img, gdFontSmall, BORDER_L + (graph->image.xplot_area * title_offset), BORDER_T - 15, string, std_colors[black]);
}


void create_graph(gdImagePtr * img, graph_t *graph) {
	/* Create the image pointer */
	if (set.verbose >= HIGH) {
		fprintf(dfp, "\nCreating %d by %d image (%s).\n", graph->image.ximg_area,
		       graph->image.yimg_area, __FUNCTION__);
	}
	*img = gdImageCreate(graph->image.ximg_area, graph->image.yimg_area);
	gdImageInterlace(*img, TRUE);
}


void draw_arrow(gdImagePtr *img, graph_t *graph) {
	int             red;
	gdPoint         points[3];
	int             xoffset, yoffset, size;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Drawing directional arrow (%s).\n", __FUNCTION__);
	red = gdImageColorAllocate(*img, 255, 0, 0);
	xoffset = BORDER_L + graph->image.xplot_area - 2;
	yoffset = BORDER_T + graph->image.yplot_area;
	size = 4;
	points[0].x = xoffset;
	points[0].y = yoffset - size;
	points[1].x = xoffset;
	points[1].y = yoffset + size;
	points[2].x = xoffset + size;;
	points[2].y = yoffset;
	gdImageFilledPolygon(*img, points, 3, red);
}


void draw_border(gdImagePtr *img, graph_t *graph) {
	int				xplot_area, yplot_area;

	xplot_area = graph->image.xplot_area;
	yplot_area = graph->image.yplot_area;

	/* Draw Plot Border */
	gdImageRectangle(*img, BORDER_L, BORDER_T, BORDER_L + xplot_area, BORDER_T + yplot_area, std_colors[black]);
	return;
}


void draw_grid(gdImagePtr *img, graph_t *graph) {
	int             styleDotted[3];
	int             img_bg;
	int             plot_bg;
	int             i;
	int				ximg_area, yimg_area;
	int				xplot_area, yplot_area;

	img_bg = gdImageColorAllocate(*img, 235, 235, 235);
	plot_bg = gdImageColorAllocate(*img, 255, 255, 255);
	ximg_area = graph->image.ximg_area;
	yimg_area = graph->image.yimg_area;
	xplot_area = graph->image.xplot_area;
	yplot_area = graph->image.yplot_area;


	if (set.verbose >= HIGH)
		fprintf(dfp, "Drawing image grid (%s).\n", __FUNCTION__);

	gdImageFilledRectangle(*img, 0, 0, ximg_area, yimg_area, img_bg);
	gdImageFilledRectangle(*img, BORDER_L, BORDER_T, BORDER_L + xplot_area, BORDER_T + yplot_area, plot_bg);

	/* draw the image border */
	gdImageLine(*img, 0, 0, ximg_area - 1, 0, std_colors[light]);
	gdImageLine(*img, 1, 1, ximg_area - 2, 1, std_colors[light]);
	gdImageLine(*img, 0, 0, 0, yimg_area - 1, std_colors[light]);
	gdImageLine(*img, 1, 1, 1, yimg_area - 2, std_colors[light]);
	gdImageLine(*img, ximg_area - 1, 0, ximg_area - 1, yimg_area - 1, std_colors[black]);
	gdImageLine(*img, 0, yimg_area - 1, ximg_area - 1, yimg_area - 1, std_colors[black]);
	gdImageLine(*img, ximg_area - 2, 1, ximg_area - 2, yimg_area - 2, std_colors[black]);
	gdImageLine(*img, 1, yimg_area - 2, ximg_area - 2, yimg_area - 2, std_colors[black]);

	/* Define dotted style */
	styleDotted[0] = std_colors[light];
	styleDotted[1] = gdTransparent;
	styleDotted[2] = gdTransparent;
	gdImageSetStyle(*img, styleDotted, 3);

	/* Draw Image Grid Verticals */
	for (i = 1; i <= xplot_area; i++) {
		if (i % (xplot_area / XTICKS) == 0) {
			gdImageLine(*img, i + BORDER_L, BORDER_T, i + BORDER_L,
				    BORDER_T + yplot_area, gdStyled);
		}
	}
	/* Draw Image Grid Horizontals */
	for (i = 1; i <= yplot_area; i++) {
		if (i % (yplot_area / YTICKS) == 0) {
			gdImageLine(*img, BORDER_L, i + BORDER_T, BORDER_L + xplot_area,
				    i + BORDER_T, gdStyled);
		}
	}

	return;
}


void write_graph(gdImagePtr * img, char *output_file) {
	FILE           *pngout;

	if (output_file) {
		pngout = fopen(output_file, "wb");
		gdImagePng(*img, pngout);
		fclose(pngout);
	} else {
		fprintf(stdout, "Content-type: image/png\n\n");
		fflush(stdout);
		gdImagePng(*img, stdout);
	}
	gdImageDestroy(*img);
}


void init_colors(gdImagePtr * img, color_t ** colors) {
	color_t	*new = NULL;
	color_t	*last = NULL;
	int		i;
	int		red[MAXLINES] =  {0, 0, 255, 255, 255, 255, 138, 95, 173, 139};
	int		green[MAXLINES] ={235, 94, 0, 255, 185, 52, 43, 158, 255, 121};
	int		blue[MAXLINES] = {12, 255, 0, 0, 15, 179, 226, 160, 47, 94};

	if (set.verbose >= HIGH)
		fprintf(dfp, "Initializing colors (%s).\n", __FUNCTION__);
	/* Define some useful colors; first allocated is background color */
	std_colors[white] = gdImageColorAllocate(*img, 255, 255, 255);
	std_colors[black] = gdImageColorAllocate(*img, 0, 0, 0);
	std_colors[light] = gdImageColorAllocate(*img, 194, 194, 194);

	/* Allocate colors for the data lines */
	for (i = 0; i < MAXLINES; i++) {
		if ((new = (color_t *) malloc(sizeof(color_t))) == NULL) {
			fprintf(dfp, "Fatal malloc error in init_colors.\n");
		}
		new->rgb[0] = red[i];
		new->rgb[1] = green[i];
		new->rgb[2] = blue[i];
		new->shade = gdImageColorAllocate(*img, red[i], green[i], blue[i]);
		new->next = NULL;
		if (*colors != NULL) {
			last->next = new;
			last = new;
		} else {
			*colors = new;
			last = new;
		}
	}
	/* Make it a circular LL */
	new->next = *colors;
}


#ifdef HAVE_STRTOLL
long long intSpeed(MYSQL *mysql, int iid) {
#else
long intSpeed(MYSQL *mysql, int iid) {
#endif
	char            query[256];
	MYSQL_RES      *result;
	MYSQL_ROW       row;

	if (set.verbose >= HIGH)
		fprintf(dfp, "Fetching interface speed (%s).\n", __FUNCTION__);

	snprintf(query, sizeof(query), "SELECT speed FROM interface WHERE id=%d", iid);
	if (set.verbose >= DEBUG)
		fprintf(dfp, "Query String: %s\n", query);
	if (mysql_query(mysql, query)) {
		fprintf(dfp, "Query error.\n");
		return (-1);
	}
	if ((result = mysql_store_result(mysql)) == NULL) {
		fprintf(dfp, "Retrieval error.\n");
		return (-1);
	}
	row = mysql_fetch_row(result);

#ifdef HAVE_STRTOLL
	return strtoll(row[0], NULL, 0);
#else
	return strtol(row[0], NULL, 0);
#endif
}


int sizeImage(graph_t *graph) {
	graph->image.ximg_area = (unsigned int)(graph->image.xplot_area + BORDER_L + BORDER_R);
	graph->image.yimg_area = (unsigned int)(graph->image.yplot_area + BORDER_T + graph->image.border_b);
	return (1);
}


void sizeDefaults(graph_t *graph) {
	graph->image.xplot_area = XPLOT_AREA;
	graph->image.yplot_area = YPLOT_AREA;
	graph->image.border_b = BORDER_B;
	graph->image.ximg_area = XIMG_AREA;
	graph->image.yimg_area = YIMG_AREA;
}


/* Compare two data_t's */
float cmp(data_t *a, data_t *b) {
    return b->rate - a->rate;
}


/* Sort a data_t linked list */
data_t *sort_data(data_t *list, int is_circular, int is_double) {
    data_t *p, *q, *e, *tail, *oldhead;
    int insize, nmerges, psize, qsize, i;

    /* if `list' was passed in as NULL, return immediately */
    if (!list)
       return NULL;

    insize = 1;

    while (1) {
        p = list;
        oldhead = list; /* only used for circular linkage */
        list = NULL;
        tail = NULL;

        nmerges = 0;  /* count number of merges we do in this pass */

        while (p) {
            nmerges++;  /* there exists a merge to be done */
            /* step `insize' places along from p */
            q = p;
            psize = 0;
            for (i = 0; i < insize; i++) {
                psize++;
               if (is_circular)
                   q = (q->next == oldhead ? NULL : q->next);
               else
                   q = q->next;
                if (!q) break;
            }

            /* if q hasn't fallen off end, we have two lists to merge */
            qsize = insize;

            /* now we have two lists; merge them */
            while (psize > 0 || (qsize > 0 && q)) {

                /* decide whether next element of merge comes from p or q */
                if (psize == 0) {
                   /* p is empty; e must come from q. */
                   e = q; q = q->next; qsize--;
                   if (is_circular && q == oldhead) q = NULL;
               } else if (qsize == 0 || !q) {
                   /* q is empty; e must come from p. */
                   e = p; p = p->next; psize--;
                   if (is_circular && p == oldhead) p = NULL;
               } else if (cmp(p,q) <= 0) {
                   /* First element of p is lower (or same);
                    * e must come from p. */
                   e = p; p = p->next; psize--;
                   if (is_circular && p == oldhead) p = NULL;
               } else {
                   /* First element of q is lower; e must come from q. */
                   e = q; q = q->next; qsize--;
                   if (is_circular && q == oldhead) q = NULL;
               }

                /* add the next element to the merged list */
               if (tail) {
                   tail->next = e;
               } else {
                   list = e;
               }
               if (is_double) {
                   /* Maintain reverse pointers in a doubly linked list. */
                   /* e->prev = tail; */
               }
               tail = e;
            }

            /* now p has stepped `insize' places along, and q has too */
            p = q;
        }
       if (is_circular) {
           tail->next = list;
			/*
           if (is_double)
             list->prev = tail;
			*/
       } else
           tail->next = NULL;

        /* If we have done only one merge, we're finished. */
        if (nmerges <= 1)   /* allow for nmerges==0, the empty list case */
            return list;

        /* Otherwise repeat, merging lists twice the size */
        insize *= 2;
    }
}


/* Count elements in a data_t list.  We can probably get this free
   elsewhere, but use this for now */
unsigned int count_data(data_t *head) {
	int count = 0;
	data_t *ptr = NULL;

	ptr = head;
	while (ptr) {
		count++;
		ptr = ptr->next;
	}
	return count;
}


/* Return the Nth largest element in a sorted data_t list, i.e. 95th % */
data_t *return_Nth(data_t *head, int datapoints, int n) {
	int depth;
	float percent = 0.0;
	int i;

	percent = 1 - ((float) n/100);
	depth = datapoints * percent;
		
	for (i=0;i<depth;i++) {
		head = head->next;
	}
	return(head);
}


void parseCmdLine(int argc, char **argv, arguments_t * arguments, graph_t * graph) {
	int	ch;

	graph->units = malloc(sizeof(DEFAULT_UNITS));
	strncpy(graph->units, DEFAULT_UNITS, sizeof(DEFAULT_UNITS));
	arguments->tables_to_plot = 0;
	arguments->iids_to_plot = 0;
	arguments->factor = 1;
	arguments->output_file = NULL;

	while ((ch = getopt(argc, argv, "ab:c:d:f:ghi:lm:n:o:pt:u:vxy")) != EOF)
		switch ((char)ch) {
		case 'a':
			arguments->aggregate = TRUE;
			break;
		case 'b':
			graph->image.border_b = atoi(optarg);
			break;
		case 'c':
			arguments->conf_file = optarg;
			break;
		case 'd':
			arguments->percentile = atoi(optarg);
			break;
		case 'f':
			arguments->factor = atoi(optarg);
			break;
		case 'g':
			graph->gauge = TRUE;
			break;
		case 'i':
			arguments->iid[arguments->iids_to_plot] = atoi(optarg);
			arguments->iids_to_plot++;
			break;
		case 'l':
			arguments->filled = TRUE;
			break;
		case 'm':
			graph->image.xplot_area = atoi(optarg);
			break;
		case 'n':
			graph->image.yplot_area = atoi(optarg);
			break;
		case 'o':
			arguments->output_file = optarg;
			break;
		case 'p':
			graph->impulses = TRUE;
			break;
		case 't':
			arguments->table[arguments->tables_to_plot] = optarg;
			arguments->tables_to_plot++;
			break;
		case 'u':
			graph->units = optarg;
			break;
		case 'v':
			set.verbose++;
			break;
		case 'x':
			graph->range.scalex = TRUE;
			break;
		case 'y':
			graph->scaley = TRUE;
			break;
		case 'h':
		default:
			usage(argv[0]);
			break;
		}

	if (argv[optind + 1]) {
		graph->range.begin = atol(argv[optind]);
		graph->range.end = atol(argv[optind + 1]);
	} else {
		usage(argv[0]);
	}
	if (set.verbose >= LOW) {
		fprintf(dfp, "RTGplot version %s.\n", VERSION);
		fprintf(dfp, "%d table(s), %d iid(s) to plot.\n", arguments->tables_to_plot,
		       arguments->iids_to_plot);
	}
}


void parseWeb(arguments_t *arguments, graph_t *graph) {
	s_cgi	**cgiArg;
	char	*temp = NULL;
	char	*token;
	char	var[BUFSIZE];
	int		i;

	graph->units = malloc(sizeof(DEFAULT_UNITS));
	strncpy(graph->units, DEFAULT_UNITS, sizeof(DEFAULT_UNITS));
	arguments->tables_to_plot = 0;
	arguments->iids_to_plot = 0;
	arguments->factor = 1;

		cgiDebug(0, 0);
		cgiArg = cgiInit();
		if ((temp = cgiGetValue(cgiArg, "factor")))
			arguments->factor = atoi(temp);
		if ((temp = cgiGetValue(cgiArg, "percentile")))
			arguments->percentile = atoi(temp);
		if ((temp = cgiGetValue(cgiArg, "aggr"))) 
			arguments->aggregate = TRUE;
		if ((temp = cgiGetValue(cgiArg, "scalex")))
			graph->range.scalex = TRUE;
		if ((temp = cgiGetValue(cgiArg, "scaley")))
			graph->scaley = TRUE;
		if ((temp = cgiGetValue(cgiArg, "xplot")))
			graph->image.xplot_area = atoi(temp);
		if ((temp = cgiGetValue(cgiArg, "yplot")))
			graph->image.yplot_area = atoi(temp);
		if ((temp = cgiGetValue(cgiArg, "borderb")))
			graph->image.border_b = atoi(temp);
		if ((temp = cgiGetValue(cgiArg, "impulses")))
			graph->impulses = TRUE;
		if ((temp = cgiGetValue(cgiArg, "gauge")))
			graph->gauge = TRUE;
		if ((temp = cgiGetValue(cgiArg, "filled")))
			arguments->filled = TRUE;
		if ((temp = cgiGetValue(cgiArg, "debug"))) {
			snprintf(var, sizeof(var), "%s.%s", DEBUGLOG, file_timestamp());
			dfp = fopen(var, "w");
			set.verbose = atoi(temp);
		}
		graph->range.begin = atol(cgiGetValue(cgiArg, "begin"));
		graph->range.end = atol(cgiGetValue(cgiArg, "end"));
		if ((temp = cgiGetValue(cgiArg, "iid"))) {
			token = strtok(temp, "\n");
			while (token) {
				arguments->iid[arguments->iids_to_plot] = atoi(token);
				arguments->iids_to_plot++;
				token = strtok(NULL, "\n");
			}
		}
/* XXX REB - Warning: Deprecated, tN argument will go away in RTG 0.8 XXX */
		for (i = 0; i < MAXTABLES; i++) {
			snprintf(var, sizeof(var), "t%d", i + 1);
			if ((arguments->table[i] = cgiGetValue(cgiArg, var)))
				arguments->tables_to_plot++;
		}
		if ((graph->units = cgiGetValue(cgiArg, "units")) == NULL) {
			graph->units = malloc(sizeof(DEFAULT_UNITS));
			strncpy(graph->units, DEFAULT_UNITS, sizeof(graph->units));
		}
}
