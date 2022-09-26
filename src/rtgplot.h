/****************************************************************************
   Program:     $Id: rtgplot.h,v 1.20 2003/09/24 12:38:06 cbrotsos Exp $ 
   Author:      $Author: cbrotsos $
   Date:        $Date: 2003/09/24 12:38:06 $
   Orig Date:   January 15, 2002
   Description: RTG traffic grapher headers
****************************************************************************/

#ifndef _RTGPLOT_H_
#define _RTGPLOT_H_ 1

#include <gd.h>
#include <gdfonts.h>
#include <strings.h>

#define XPLOT_AREA 500
#define YPLOT_AREA 150
#define BORDER_T 20
#define BORDER_B 70
#define BORDER_L 50
#define BORDER_R 20
#define XIMG_AREA (unsigned int)(XPLOT_AREA + BORDER_L + BORDER_R)
#define YIMG_AREA (unsigned int)(YPLOT_AREA + BORDER_T + BORDER_B)

#define KILO 1000
#define MEGA (unsigned int)(KILO * KILO)
#define GIGA (unsigned long long)(MEGA * KILO)
#define TERA (unsigned long long)(GIGA * KILO)

#define XTICKS 10
#define YTICKS 5

#define MINUTE 60
#define HOUR (unsigned int)(MINUTE * 60)
#define DAY (unsigned int)(HOUR * 24)
#define WEEK (unsigned int)(DAY * 7)
#define MONTH (unsigned int)(WEEK * 4)

#define DEFAULT_UNITS "bps"
#define MAXLINES 10
#define MAXTABLES 3
#define MAXIIDS 5

#define COPYRIGHT "RTG"
#define DEBUGLOG "/tmp/rtgplot.log"


/* Populate a data_t per item to plot */
typedef struct data_struct {
    long long counter;		// interval sample value
    unsigned long timestamp;	// UNIX timestamp
    float rate;			// floating point rate
    int x;			// X plot coordinate
    int y;			// Y plot coordinate
    struct data_struct *next;	// next sample
} data_t;

/* If calculating rate, a rate_t stores total, max, avg, cur rates */
typedef struct rate_struct {
    unsigned long long total;
    float max;
    float avg;
    float cur;
} rate_t;

/* Each graph uses a range_t to keep track of data ranges */
typedef struct range_struct {
    unsigned long begin;	// UNIX begin time
    unsigned long end;		// UNIX end time
    unsigned long dataBegin;	// Actual first datapoint in database
    long long counter_max;	// Largest counter in range
    int scalex;			// Scale X values to match actual datapoints
    long datapoints;		// Number of datapoints in range
} range_t;

/* Each graph has a image_t struct to keep borders and area variables */
typedef struct image_struct {
    unsigned int xplot_area;	// X pixels of the plot
    unsigned int yplot_area;	// Y pixels of the plot
    unsigned int border_b;	// Pixels of border from plot to image bottom
    unsigned int ximg_area;	// X pixels of the entire image
    unsigned int yimg_area;	// Y pixels of the entire image
} image_t;

/* Each graph uses a graph_t struct to keep graph properties */
typedef struct graph_struct {
    int xmax;
    float ymax;
    unsigned long xoffset;
    int xunits;
    long long yunits;
    char *units;
    int impulses;
    int gauge;
    int scaley; 
    range_t range;
    image_t image;
} graph_t;

/* A linked list of colors that we iterate through each line */
typedef struct color_struct {
    int shade;
    int rgb[3];
    struct color_struct *next;
} color_t;

typedef struct arguments_struct {
    char *table[MAXTABLES];
    int iid[MAXIIDS];
    int tables_to_plot;
    int iids_to_plot;
    int factor;
    unsigned int aggregate;
    unsigned int percentile;
    unsigned int filled;
    char *conf_file;
    char *output_file;
} arguments_t;

/* Globals */
enum major_colors {white, black, light};
int std_colors[3];


/* Precasts: rtgplot.c */
void dump_data(data_t *);
int populate(char *, MYSQL *, data_t **, graph_t *);
void normalize(data_t *, graph_t *);
void usage(char *);
void dump_rate_stats(rate_t *);
void plot_line(data_t *, gdImagePtr *, graph_t *, int, int);
void plot_Nth(gdImagePtr *, graph_t *, data_t *);
void create_graph(gdImagePtr *, graph_t *);
void draw_grid(gdImagePtr *, graph_t *);
void draw_border(gdImagePtr *, graph_t *);
void draw_arrow(gdImagePtr *, graph_t *);
void write_graph(gdImagePtr *, char *);
void plot_scale(gdImagePtr *, graph_t *);
void plot_labels(gdImagePtr *, graph_t *);
void plot_legend(gdImagePtr *, rate_t, graph_t *, int, char *, int);
void init_colors(gdImagePtr *, color_t **);
void calculate_rate(data_t **, rate_t *, int);
void calculate_total(data_t **, rate_t *, int);
#ifdef HAVE_STRTOLL
long long intSpeed(MYSQL *, int);
#else
long intSpeed(MYSQL *, int);
#endif
void sizeDefaults(graph_t *);
int sizeImage(graph_t *);
float cmp(data_t *, data_t *);
data_t *sort_data(data_t *data, int is_circular, int is_double );
unsigned int count_data(data_t *);
data_t *return_Nth(data_t *, int, int);
void parseCmdLine(int, char **, arguments_t *, graph_t *);
void parseWeb(arguments_t *, graph_t *);
void dataAggr(data_t *, data_t *, rate_t *, rate_t *, graph_t *);
char *file_timestamp();

#endif /* not _RTGPLOT_H_ */
