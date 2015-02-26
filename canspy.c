#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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

#define NAME "canspy"

/* program options */
static const char help_msg[] =
	NAME ": CAN spy\n"
	"usage:	" NAME " [OPTIONS ...] DEVICE ID[/MASK] ...\n"
	"\n"
	"Options\n"
	" -V, --version		Show version\n"
	" -v, --verbose		Verbose output\n"
	"\n"
	;
#ifdef _GNU_SOURCE
static struct option long_opts[] = {
	{ "help", no_argument, NULL, '?', },
	{ "version", no_argument, NULL, 'V', },
	{ "verbose", no_argument, NULL, 'v', },
	{ },
};
#else
#define getopt_long(argc, argv, optstring, longopts, longindex) \
	getopt((argc), (argv), (optstring))
#endif
static const char optstring[] = "V?v";
static int verbose;

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
};

static int cmpcache(const void *va, const void *vb)
{
	const struct cache *a = va, *b = vb;

	return a->cf.can_id - b->cf.can_id;
}

static inline void sort_cache(struct cache *cache, size_t n)
{
	qsort(cache, n, sizeof(*cache), cmpcache);
}

static int cf_classify(const struct can_frame *cf, const struct cache *cache, int n)
{
	int idx, top = n, bot = 0;

	bot = 0;
	top = n-1;
	while (bot <= top) {
		idx = (top+bot)/2;
		if (cf->can_id == cache[idx].cf.can_id)
			return idx;
		else if (cf->can_id < cache[idx].cf.can_id)
			top = idx -1;
		else
			bot = idx +1;
	}
	/* not found */
	return n;
}

int main(int argc, char *argv[])
{
	int opt, ret, sock, j, byte, ndirty;
	const char *device;
	char *endp;
	struct can_filter *filters;
	size_t nfilters, sfilters;
	struct sockaddr_can addr = { .can_family = AF_CAN, };
	struct can_frame cf;
	struct cache *cache;
	size_t ncache, scache;
	double last_update;

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
		ret = recv(sock, &cf, sizeof(cf), 0);
		if (ret < 0)
			error(1, errno, "recv %s", device);
		if (!ret)
			break;

		ndirty = 0;
		ret = cf_classify(&cf, cache, ncache);
		if (ret >= scache) {
			scache += 16;
			cache = realloc(cache, sizeof(*cache)*scache);
			for (j = ncache; j < scache; ++j)
				/* set all bits, including CAN_ERR_FLAG */
				cache[j].flags = 0;

		}
		if (ret >= ncache) {
			/* store in cache */
			cache[ncache].cf = cf;
			cache[ncache].flags |= F_DIRTY;
			++ncache;
			sort_cache(cache, ncache);
			++ndirty;
		} else {
			if ((cache[ret].cf.can_id != cf.can_id) ||
					(cache[ret].cf.can_dlc != cf.can_dlc) ||
					memcmp(cache[ret].cf.data, cf.data, cf.can_dlc)) {
				cache[ret].flags |= F_DIRTY;
				++ndirty;
				cache[ret].cf = cf;
			} else {
				continue;
			}
		}
		update_jiffies();
		if ((jiffies - last_update) < 0.25)
			continue;
		ndirty = 0;
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
			printf("\n");
			cache[j].flags &= F_DIRTY;
		}
	}
	return 0;
}

