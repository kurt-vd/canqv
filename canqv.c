#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <sys/time.h>

#include <error.h>
#include <getopt.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>

/* terminal codes */

#define CLR_SCREEN  "\33[2J"
#define CSR_HOME  "\33[H"
#define ATTRESET "\33[0m"

#define NAME "canqv"

/* program options */
static const char help_msg[] =
	NAME ": CAN spy\n"
	"usage:	" NAME " [OPTIONS ...] DEVICE ID[/MASK] ...\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	"\n"
	" -m, --maxperiod=TIME	Consider TIME as maximum period (default 2s).\n"
	"			Slower rates are considered multiple one-time ID's\n"
	" -x, --remove=TIME	Remove ID's after TIME (default 10s).\n"
	"\n"
	;
#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },

	{ "remove", required_argument, NULL, 'x', },
	{ "maxperiod", required_argument, NULL, 'm', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "V?vx:m:";
static int verbose;
static double deadtime = 10.0;
static double maxperiod = 2.0;

/* jiffies, in msec */
static double jiffies;

static void update_jiffies(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	jiffies = tv.tv_sec + tv.tv_usec/1e6;
}

/* cache definition */
struct cache {
	struct can_frame cf;
	int flags;
		#define F_DIRTY		0x01
	double lastrx;
	double period;
};

static int cmpcache(const void *va, const void *vb)
{
	const struct cache *a = va, *b = vb;

	return a->cf.can_id - b->cf.can_id;
}

int main(int argc, char *argv[])
{
	int opt, ret, sock, j, byte;
	const char *device;
	char *endp;
	struct can_filter *filters;
	size_t nfilters, sfilters;
	struct sockaddr_can addr = { .can_family = AF_CAN, };
	struct cache *cache, w, *curr;
	size_t ncache, scache;
	double last_update, lastseen;

	/* argument parsing */
	while ((opt = getopt_long(argc, argv, optstring, long_opts, NULL)) != -1)
	switch (opt) {
	case 'V':
		fprintf(stderr, "%s %s, "
				"Compiled on %s %s\n",
				NAME, VERSION, __DATE__, __TIME__);
		exit(0);
		break;
	default:
		fprintf(stderr, "%s: unknown option '%u'\n\n", NAME, opt);
	case '?':
		fputs(help_msg, stderr);
		return opt != '?';
	case 'v':
		++verbose;
		break;
	case 'x':
		deadtime = strtod(optarg, NULL);
		break;
	case 'm':
		maxperiod = strtod(optarg, NULL);
		break;
	}

	/* parse CAN device */
	if (argv[optind]) {
		addr.can_ifindex = if_nametoindex(argv[optind]);
		if (!addr.can_ifindex)
			error(1, errno, "device '%s' not found", argv[optind]);
		device = argv[optind];
		++optind;
	} else
		device = "any";

	/* parse filters */
	filters = NULL;
	sfilters = nfilters = 0;
	for (; optind < argc; ++optind) {
		if (nfilters >= sfilters) {
			sfilters += 16;
			filters = realloc(filters, sizeof(*filters) * sfilters);
			if (!filters)
				error(1, errno, "realloc");
		}
		filters[nfilters].can_id = strtoul(argv[optind], &endp, 16);
		if ((endp - argv[optind]) > 3)
			filters[nfilters].can_id |= CAN_EFF_MASK;
		if (strchr(":/", *endp))
			filters[nfilters].can_mask = strtoul(endp+1, NULL, 16) |
				CAN_EFF_FLAG | CAN_RTR_FLAG;
		else
			filters[nfilters].can_mask = CAN_EFF_MASK |
				CAN_EFF_FLAG | CAN_RTR_FLAG;
		++nfilters;
	}

	/* prepare socket */
	sock = ret = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (ret < 0)
		error(1, errno, "socket PF_CAN");

	if (nfilters) {
		ret = setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, filters,
				nfilters * sizeof(*filters));
		if (ret < 0)
			error(1, errno, "setsockopt %li filters", nfilters);
	}

	ret = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0)
		error(1, errno, "bind %s", device);

	/* pre-init cache */
	scache = ncache = 0;
	cache = NULL;

	last_update = 0;
	while (1) {
		ret = recv(sock, &w.cf, sizeof(w.cf), 0);
		if (ret < 0)
			error(1, errno, "recv %s", device);
		if (!ret)
			break;

		update_jiffies();
		curr = bsearch(&w, cache, ncache, sizeof(*cache), cmpcache);
		if (!curr && (ncache >= scache)) {
			/* grow cache */
			scache += 16;
			cache = realloc(cache, sizeof(*cache)*scache);
			memset(cache+ncache, 0, (scache - ncache)*sizeof(*cache));
		}
		if (!curr) {
			/* add in cache */
			curr = cache+ncache++;
			curr->flags |= F_DIRTY;
			curr->cf = w.cf;
			curr->period = NAN;
			curr->lastrx = jiffies;
			qsort(cache, ncache, sizeof(*cache), cmpcache);
		} else {
			if ((curr->cf.can_id != w.cf.can_id) ||
				(curr->cf.can_dlc != w.cf.can_dlc) ||
				memcmp(curr->cf.data, w.cf.data, w.cf.can_dlc))
				curr->flags |= F_DIRTY;
			/* update cache */
			curr->cf = w.cf;
			curr->period = jiffies - curr->lastrx;
			if (curr->period > maxperiod)
				curr->period = NAN;
			curr->lastrx = jiffies;
		}

		if ((jiffies - last_update) < 0.25)
			continue;
		/* remove dead cache */
		for (j = 0; j < ncache; ++j) {
			curr = cache+j;
			lastseen = jiffies - curr->lastrx;

			if (lastseen > deadtime) {
				/* delete this entry */
				memcpy(cache+j, cache+j+1, (ncache-j-1)*sizeof(*cache));
				--ncache;
				--j;
				continue;
			}

			if (!isnan(curr->period) && (lastseen > 2*curr->period))
				/* reset period */
				curr->period = NAN;
		}

		last_update = jiffies;
		/* update screen */
		puts(CLR_SCREEN ATTRESET CSR_HOME);
		for (j = 0; j < ncache; ++j) {
			if (cache[j].cf.can_id & CAN_EFF_FLAG)
				printf("%08x:", cache[j].cf.can_id & CAN_EFF_MASK);
			else
				printf("     %03x:", cache[j].cf.can_id & CAN_SFF_MASK);
			for (byte = 0; byte < cache[j].cf.can_dlc; ++byte)
				printf(" %02x", cache[j].cf.data[byte]);
			for (; byte < 8; ++byte)
				printf(" --");
			printf("\tlast=-%.3lfs", jiffies - cache[j].lastrx);
			if (!isnan(cache[j].period))
				printf("\tperiod=%.3lfs", cache[j].period);
			printf("\n");
			cache[j].flags &= F_DIRTY;
		}
	}
	return 0;
}

