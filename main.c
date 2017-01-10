#include "network.h"
#include "dhcp.h"

#include <string.h>
#include <getopt.h>

static const char   *prgname = "netconfig";

typedef struct config
{
    char        *eth;
    char        *ip;
    char        *mask;
    char        *bcast;
    char        *gw;
    char        *ns;
    int         save:1,
                dhcp:1,
                all:1;
} config_t;

static const struct option  long_options [] =
{
    {"help",    no_argument,        NULL,   0},
    {"dhcp",    no_argument,        NULL,   0},
    {"eth",     required_argument,  NULL,   0},
    {"ip",      required_argument,  NULL,   0},
    {"mask",    required_argument,  NULL,   0},
    {"bcast",   required_argument,  NULL,   0},
    {"gw",      required_argument,  NULL,   0},
    {"ns",      required_argument,  NULL,   0},
    {"save",    no_argument,        NULL,   0},
    {"csv",     no_argument,        NULL,   0},
    {"all",     no_argument,        NULL,   0},

    {0,         0,                  0,      0},
};

static int csv_display(const struct ifreq *ifr, void *unused);
static int display(const struct ifreq *ifr, void *unused);

typedef int (* display_t)(const struct ifreq *ifr, void *unused);
static display_t display_func = &display;

static void usage(const char *prg)
{
    fprintf(stderr, "Display or set network informations\n");
    fprintf(stderr, "usage: %s [options] [interface]\n", prg);
    fprintf(stderr, "options:\n");
    fprintf(stderr, "\t--help  |-h           : display this and exit\n");
    fprintf(stderr, "\t--dhcp  |-d           : dhcp mode\n");
    fprintf(stderr, "\t--eth   |-e <ethaddr> : set MAC address\n");
    fprintf(stderr, "\t--ip    |-i <ip>      : set ip address\n");
    fprintf(stderr, "\t--mask  |-m <mask>    : set ip mask address\n");
    fprintf(stderr, "\t--bcast |-b <addr>    : set broadcast address\n");
    fprintf(stderr, "\t--gw    |-g <gw>      : set the default gateway\n");
    fprintf(stderr, "\t--ns    |-n <server>  : set the name server\n");
    fprintf(stderr, "\t--save  |-s           : save the configuration\n");
    fprintf(stderr, "\t--csv   |-c           : output display as a CSV\n");
    fprintf(stderr, "\t--all   |-a           : consider all of the interfaces\n");
}

static int parse_long_options(const char *opt)
{
    if ( !strcmp(opt, "help") ||
            !strcmp(opt, "dhcp") ||
            !strcmp(opt, "eth") ||
            !strcmp(opt, "ip") ||
            !strcmp(opt, "mask") ||
            !strcmp(opt, "bcast") ||
            !strcmp(opt, "gw") ||
            !strcmp(opt, "ns") ||
            !strcmp(opt, "save") ||
            !strcmp(opt, "csv") ||
            !strcmp(opt, "all") ||
            !strcmp(opt, "dhcp") )
        return opt[0];

    fprintf(stderr, "Unknown option --%s\n", opt);

    return -1;
}

static int parse_options(int argc, char * const argv[], config_t *conf)
{
    int     c,
            index;

    memset(conf, 0, sizeof(config_t));

    while ( (c = getopt_long(argc, argv, "hde:i:m:b:g:n:sca",
                    long_options, &index)) != -1 )
    {
        if ( c == 0 &&
                (c = parse_long_options(long_options[index].name)) < 0 )
            return -1;

        switch ( c )
        {
            case 'h':
            usage(prgname);
            return 0;

            case 'd':
            conf->dhcp++;
            break;

            case 'e':
            conf->eth = optarg;
            break;

            case 'i':
            conf->ip = optarg;
            break;

            case 'm':
            conf->mask = optarg;
            break;

            case 'b':
            conf->bcast = optarg;
            break;

            case 'g':
            conf->gw = optarg;
            break;

            case 'n':
            conf->ns = optarg;
            break;

            case 's':
            conf->save++;
            break;

            case 'c':
            display_func = &csv_display;
            break;

            case 'a':
            conf->all++;
            break;

            default:
            fprintf(stderr, "unknow option -%c\n", c);
            return -1;
        }
    }

    return 1;
}

static int csv_display(const struct ifreq *ifr, void *unused)
{
    char    str[NI_MAXHOST];

    static int header = 0;

    if ( !header )
    {
        printf("if,plug,dyn,mac,ip,mask,bcast,gw,ns\n");;
        header++;
    }

    printf("%s,", ifr->ifr_name);

    printf("%d,", isInterfacePlugged(ifr) ? 1 : 0);

    printf("%d,", isInterfaceDynamic(ifr->ifr_name) ?
                1 : 0);

    if ( getMacAddress(ifr, str, sizeof(str)) == 0 )
        printf("%s", str);

    printf(",");
    if ( getIpAddress(ifr, str, sizeof(str)) == 0 )
        printf("%s", str);

    printf(",");
    if ( getIpMask(ifr, str, sizeof(str)) == 0 )
        printf("%s", str);

    printf(",");
    if ( getIpBroadcast(ifr, str, sizeof(str)) == 0 )
        printf("%s", str);

    printf(",");
    if ( getIpGateway(ifr, str, sizeof(str)) == 0 )
        printf("%s", str);

    printf(",");
    if ( getDomainNameServer(str, sizeof(str)) == 0 )
        printf("%s", str);

    printf("\n");

    return 0;
}

static int display(const struct ifreq *ifr, void *unused)
{
    printf("%s ", ifr->ifr_name);

	saveInterfaceIpConfig(ifr, MANUAL);
    return 0;
}

int main(int argc, char *argv[])
{
    int ret = -1;

    if ( networkInit() )
    {
        fprintf(stderr, "Cannot init\n");
        return -1;
    }

    addAllInterfaces();

	ret = foreachInterfaceIpv4(display_func, NULL);
    return ret;
}
