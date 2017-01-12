#include "network.h"
#include "dhcp.h"

/* See man (7) netdevice for IOCTL's interface
 * See man (3) rtnetlink for RTA_XXX
 * See man (3) netlink for NLMSG_XXX
 */

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/route.h>
#include <netinet/ether.h>

#include <string.h>
#include <unistd.h>
#include <ctype.h>

#define NBIFACE     32

/* FIXME
 * - Full compatibility with Ipv6
 */
static int              init = 0;

static struct ifconf    ifconf;
static struct ifreq     ifreqs[NBIFACE];

static int              fd;

static const char       *interface = "/mnt/boot/conf/interfaces";
static const char       *tmpInterface = "/mnt/boot/conf/interfaces.tmp";

static const char       *resolv = "/etc/resolv.conf";
static const char       *tmpResolv = "/etc/resolv.conf.tmp";

static const char       *devices = "/proc/net/dev";

typedef struct route_info
{
    struct in_addr  dstAddr;
    struct in_addr  gateway;
    char            ifName[IF_NAMESIZE];
} route_info_t;

static inline int getFileDescriptor(void)
{
    return socket(AF_INET, SOCK_DGRAM, 0);
}

static inline void closeFileDescriptor(int fd)
{
    close(fd);
}

int getIfaceList(struct ifconf *ifc)
{
    int ret;

    if ( ifc == NULL )
        return -1;

    if ( (ret = ioctl(fd, SIOCGIFCONF, ifc)) < 0 )
        perror("ioctl failed");

    return ret;
}

int networkInit(void)
{
    if ( init )
        return 0;

    if ( (fd = getFileDescriptor()) < 0 )
        return -1;

    /* Get the list for the devices
     */
    ifconf.ifc_buf = (char *) ifreqs;
    ifconf.ifc_len = sizeof(ifreqs);
    if ( getIfaceList(&ifconf) < 0 )
    {
        closeFileDescriptor(fd);
        return -1;
    }

    init = 1;

    return 0;
}

static char *trimSpace(char *str)
{
    if ( !str )
        return NULL;

    while ( str )
    {
        if ( !isspace(*str) )
            return str;

        str++;
    }

    return NULL;
}

int addAllInterfaces(void)
{
#define BUFFLEN     1024
    FILE    *dev;
    char    buff[BUFFLEN];
    int     ret = 0;

    if ( !init )
        return -1;

    if ( (dev = fopen(devices, "r")) == NULL )
        return -1;

    while ( fgets(buff, sizeof(buff), dev) )
    {
        char    *p;

        if ( (p = strstr(buff, ":")) == NULL )
            continue;

        *p = '\0';
        ret |= getInterfaceByName(trimSpace(buff), AF_INET) != NULL ? 0 : 1;
        if ( ret )
            break;
    }

    fclose(dev);

    return ret;
#undef BUFFLEN
}

void networkClean(void)
{
    if ( init )
    {
        close(fd);
        init = 0;
    }
}

const struct ifreq *getInterfaceByName(const char *ifname, int domain)
{
    char                *ptr;
    const struct ifreq  *ifr;
    struct ifreq        dummy;
    unsigned            nb = 0;

    if ( !init )
    {
        fprintf(stderr, "network uninitialized !\n");
        return NULL;
    }

    if ( ifname == NULL )
        return NULL;

    switch ( domain )
    {
        case AF_INET:
        case AF_INET6:
        break;

        default:
        return NULL;
    }

    for ( ptr = ifconf.ifc_buf ; ptr < ifconf.ifc_len + ifconf.ifc_buf ; ptr += sizeof(struct ifreq) )
    {
        ifr = (const struct ifreq *) ptr;

        if ( ifr->ifr_addr.sa_family != domain )
            continue;

        if ( strncmp(ifname, ifr->ifr_name, IFNAMSIZ) == 0 )
            return ifr;

        nb++;
    }

    /* Dirty hack
     * If the interface is down, SIOCGIFCONF does not see it !
     */
    strncpy(dummy.ifr_name, ifname, IFNAMSIZ);
    if( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl");
        return NULL;
    }

    /* Check overflow and insert it in the list if found
     */
    if ( ++nb > NBIFACE )
    {
        fprintf(stderr, "Too many interfaces");
        return NULL;
    }

    dummy.ifr_addr.sa_family = AF_INET;
    memcpy(ptr, &dummy, sizeof(struct ifreq));
    ifconf.ifc_len += sizeof(struct ifreq);
    return (const struct ifreq *) ptr;
}

int isInterfacePlugged(const struct ifreq *ifr)
{
    struct ifreq    dummy;

    if ( ifr == NULL )
        return 0;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return (dummy.ifr_flags & IFF_UP) &&
            (dummy.ifr_flags & IFF_RUNNING);
}

int getIpAddress(const struct ifreq *ifr, char *dest, size_t len)
{
    struct ifreq    dummy;

    if ( ifr == NULL || dest == NULL || len < INET_ADDRSTRLEN )
        return -1;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    /* Check if the interface is UP and RUNNING to ask an address
     */
    if ( !((dummy.ifr_flags & IFF_UP) &&
            (dummy.ifr_flags & IFF_RUNNING)) )
        return -1;

    if ( ioctl(fd, SIOCGIFADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return getnameinfo(&dummy.ifr_addr, sizeof(struct sockaddr_in),
            dest, len, NULL, 0, NI_NUMERICHOST);
}

int setInterfaceIpAddress(const struct ifreq *ifr, const char *ip)
{
    struct sockaddr_in		sin;
    struct in_addr			in;
    struct ifreq			dummy;

    if ( ifr == NULL || ip == NULL )
        return -1;

    /* Grab the address in network order
     */
    if ( inet_aton(ip, &in) == 0 )
        return -2;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    memset(&sin, 0, sizeof(struct sockaddr));
    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    sin.sin_addr.s_addr = in.s_addr;
    memcpy((char *)&dummy + offsetof(struct ifreq, ifr_addr), &sin, sizeof(struct sockaddr));

    if ( ioctl(fd, SIOCSIFADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

int getMacAddress(const struct ifreq *ifr, char *dest, size_t len)
{
    struct ifreq              dummy;
    const struct ether_addr   *eth;

    if ( ifr == NULL || dest == NULL || len < INET_ADDRSTRLEN )
        return -1;

    /* Do not count loopback
     */
    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    if ( (dummy.ifr_flags & IFF_LOOPBACK) )
        return -2;

    if ( ioctl(fd, SIOCGIFHWADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    eth = (const struct ether_addr *) dummy.ifr_hwaddr.sa_data;

    snprintf(dest, len, "%02x:%02x:%02x:%02x:%02x:%02x",
                eth->ether_addr_octet[0],
                eth->ether_addr_octet[1],
                eth->ether_addr_octet[2],
                eth->ether_addr_octet[3],
                eth->ether_addr_octet[4],
                eth->ether_addr_octet[5]);

    return 0;
}

int setInterfaceMacAddress(const struct ifreq *ifr, const char *mac)
{
    struct ifreq              dummy;
    struct ether_addr         *eth;

    if ( ifr == NULL || mac == NULL )
        return -1;

    /* ether_aton returns a pointer to a statically allocated buffer
     */
    if ( (eth = ether_aton(mac)) == NULL )
        return -1;

    /* Do not count loopback
     */
    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    if ( (dummy.ifr_flags & IFF_LOOPBACK) )
        return -2;

    memcpy(dummy.ifr_hwaddr.sa_data, eth, sizeof(struct ether_addr));
    if ( ioctl(fd, SIOCSIFHWADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

int getIpMask(const struct ifreq *ifr, char *dest, size_t len)
{
    struct ifreq    dummy;

    if ( ifr == NULL || dest == NULL || len < INET_ADDRSTRLEN )
        return -1;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    /* Check if the interface is UP and RUNNING to ask an address
     */
    if ( !((dummy.ifr_flags & IFF_UP) &&
            (dummy.ifr_flags & IFF_RUNNING)) )
        return -1;

    if ( ioctl(fd, SIOCGIFNETMASK, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return getnameinfo(&dummy.ifr_netmask, sizeof(struct sockaddr_in),
            dest, len, NULL, 0, NI_NUMERICHOST);
}

int setInterfaceIpMask(const struct ifreq *ifr, const char *mask)
{
    struct sockaddr_in		sin;
    struct ifreq			dummy;

    if ( ifr == NULL || mask == NULL )
        return -1;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFNETMASK, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    /* Grab the address in network order.
     * Here, we use inet_addr rather than inet_aton because
     * the 255.255.255.255 is valid !
     */
    memset(&sin, 0, sizeof(struct sockaddr));
    if ( (sin.sin_addr.s_addr = inet_addr(mask)) == INADDR_NONE )
        return -2;

    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    memcpy((char *)&dummy + offsetof(struct ifreq, ifr_netmask), &sin, sizeof(struct sockaddr));

    if ( ioctl(fd, SIOCSIFNETMASK, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

int getIpBroadcast(const struct ifreq *ifr, char *dest, size_t len)
{
    struct ifreq			dummy;

    if ( ifr == NULL || len < INET_ADDRSTRLEN )
        return -1;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFFLAGS, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    /* Check if the interface is UP and RUNNING to ask an address
     */
    if ( !((dummy.ifr_flags & IFF_UP) &&
            (dummy.ifr_flags & IFF_RUNNING)) )
        return -1;

    if ( ioctl(fd, SIOCGIFBRDADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return getnameinfo(&dummy.ifr_broadaddr, sizeof(struct sockaddr_in),
            dest, len, NULL, 0, NI_NUMERICHOST);
}

int setInterfaceIpBroadcast(const struct ifreq *ifr, const char *bcast)
{
    struct sockaddr_in		sin;
    struct ifreq			dummy;

    if ( ifr == NULL )
        return -1;

    strncpy(dummy.ifr_name, ifr->ifr_name, IFNAMSIZ);
    if ( ioctl(fd, SIOCGIFBRDADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    /* Grab the address in network order.
     * Here, we use inet_addr rather than inet_aton because
     * the 255.255.255.255 is valid !
     */
    memset(&sin, 0, sizeof(struct sockaddr));
    if ( (sin.sin_addr.s_addr = inet_addr(bcast)) == INADDR_NONE )
        return -2;

    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    memcpy((char *)&dummy + offsetof(struct ifreq, ifr_broadaddr), &sin, sizeof(struct sockaddr));

    if ( ioctl(fd, SIOCSIFBRDADDR, &dummy) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

static int getRouteInfo(const char *buff, size_t len, route_info_t *ri)
{
    const struct nlmsghdr   *nlMsg;
    const struct rtattr     *rtAttr;
    const struct rtmsg      *rtMsg;
    int                     rtLen;

    nlMsg = (const struct nlmsghdr *) buff;
    rtMsg = (struct rtmsg *) NLMSG_DATA(nlMsg);
    if ( rtMsg->rtm_family != AF_INET ||
            rtMsg->rtm_table != RT_TABLE_MAIN )
        return -1;

    rtAttr = (struct rtattr *) RTM_RTA(rtMsg);
    rtLen = RTM_PAYLOAD(nlMsg);

    /* We have no warranty that the attributes appear in order.
     */
    for ( ; RTA_OK(rtAttr, rtLen) ; rtAttr = RTA_NEXT(rtAttr, rtLen) )
    {
        switch ( rtAttr->rta_type )
        {
            case RTA_OIF:
            if_indextoname(*(const int*)RTA_DATA(rtAttr), ri->ifName);
            break;

            case RTA_GATEWAY:
            memcpy(&ri->gateway, RTA_DATA(rtAttr), sizeof(ri->gateway));
            break;

            case RTA_DST:
            memcpy(&ri->dstAddr, RTA_DATA(rtAttr), sizeof(ri->dstAddr));
            break;

            default:
            /* We don't really care about them...
             */
            break;
        }
    }

    return 0;
}

int getIpGateway(const struct ifreq *ifr, char *dest, size_t len)
{
/* 1 kB is not enough too handle the whole response
 */
#define BUFFLEN     (1024*3)
    char            buff[BUFFLEN];
    int             sock,
                    rlen,
                    msgLen;
    struct nlmsghdr *nlMsg;
    route_info_t    ri;
    static uint32_t seqnum = 0;
    pid_t           pid;

    /* FIXME
     * for Ipv6, it is INET6_ADDRSTRLEN
     */
    if ( ifr == NULL || dest == NULL || len < INET_ADDRSTRLEN )
        return -1;

    /* Create a new socket
     */
    if ( (sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) < 0 )
    {
        perror("socket");
        return -1;
    }

    /* Init
     */
    memset(buff, 0, BUFFLEN);
    nlMsg = (struct nlmsghdr *) buff;

    /* Prepare the request
     */
    nlMsg->nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    nlMsg->nlmsg_type = RTM_GETROUTE;
    nlMsg->nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
    nlMsg->nlmsg_seq = seqnum++;
    pid = nlMsg->nlmsg_pid = getpid();

    if ( send(sock, nlMsg, nlMsg->nlmsg_len, 0) < 0 )
    {
        perror("send");
        close(sock);
        return -1;
    }

    /* Read the answer :
     */
    msgLen = 0;
    memset(buff, 0, BUFFLEN);
    do
    {
        if ( (rlen = recv(sock, buff + msgLen,
                        BUFFLEN - msgLen, 0)) < 0 )
        {
            perror("recv");
            close(sock);
            return -1;
        }

        nlMsg = (struct nlmsghdr *) (buff + msgLen);
        if ( NLMSG_OK(nlMsg, rlen) == 0 ||
                nlMsg->nlmsg_type == NLMSG_ERROR )
        {
            fprintf(stderr, "buffer too small\n");
            close(sock);
            return -1;
        }

        if ( nlMsg->nlmsg_type == NLMSG_DONE )
            break;

        msgLen += rlen;

        if ( (nlMsg->nlmsg_flags & NLM_F_MULTI) == 0 )
            break;
    }
    while ( nlMsg->nlmsg_seq != seqnum-1 || nlMsg->nlmsg_pid != pid );

    /* Parse the messages and extract routes
     */
    for ( nlMsg = (struct nlmsghdr *) buff ; NLMSG_OK(nlMsg, msgLen) ; nlMsg = NLMSG_NEXT(nlMsg, msgLen) )
    {
        memset(&ri, 0, sizeof(ri));

        if ( getRouteInfo((const char *)nlMsg, msgLen, &ri) != 0 )
            continue;

        /* We only care about the default gateway
         */
        if ( ri.dstAddr.s_addr != INADDR_ANY )
            continue;

        if ( strncmp(ifr->ifr_name, ri.ifName, sizeof(ri.ifName)) != 0 )
            continue;

        /* Overflow is already check at the begining
         */
        memset(dest, 0, len);

        return inet_ntop(AF_INET, &ri.gateway, dest, len) != NULL
            ? 0 : 1;
    }

    return -1;
#undef BUFFLEN
}

static void prepareRouteEntry(const struct in_addr *inp, const struct ifreq *ifr, struct rtentry *rt)
{
    struct sockaddr_in  *in;

    memset(rt, 0, sizeof(*rt));

    in = (struct sockaddr_in *) &rt->rt_gateway;
    in->sin_family = AF_INET;
    in->sin_addr = *inp;

    in = (struct sockaddr_in *) &rt->rt_dst;
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = INADDR_ANY;

    in = (struct sockaddr_in *) &rt->rt_genmask;
    in->sin_family = AF_INET;
    in->sin_addr.s_addr = INADDR_ANY;

    /* FIXME
     * avoid cast and copy the content
     */
    if ( ifr )
        rt->rt_dev = (char*) ifr->ifr_name;

    rt->rt_flags = RTF_UP | RTF_GATEWAY;
}

int setInterfaceIpGateway(const struct ifreq *ifr, const char *gw)
{
    struct rtentry  route;
    struct in_addr  ina;

    if ( ifr == NULL || gw == NULL )
        return -1;

    if ( inet_aton(gw, &ina) != 1 )
        return -1;

    prepareRouteEntry(&ina, ifr, &route);

    if ( ioctl(fd, SIOCADDRT, &route) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

int delInterfaceIpGateway(const struct ifreq *ifr, const char *gw)
{
    struct rtentry      route;
    struct in_addr      ina;

    if ( ifr == NULL || gw == NULL )
        return -1;

    if ( inet_aton(gw, &ina) != 1 )
        return -1;

    prepareRouteEntry(&ina, ifr, &route);

    if ( ioctl(fd, SIOCDELRT, &route) < 0 )
    {
        perror("ioctl failed");
        return -1;
    }

    return 0;
}

static int saveInterfaceIpConfigManual(FILE *file, const struct ifreq *ifr)
{
    /* See man interfaces
     */
    char    str[INET_ADDRSTRLEN];

    if ( ifr == NULL )
        return -1;

    fprintf(file, "auto %s\n", ifr->ifr_name);
    fprintf(file, "iface %s inet static\n", ifr->ifr_name);

    if ( getIpAddress(ifr, str, sizeof(str)) == 0 )
        fprintf(file, "\taddress %s\n", str);

    if ( getIpMask(ifr, str, sizeof(str)) == 0 )
        fprintf(file, "\tnetmask %s\n", str);

    if ( getIpBroadcast(ifr, str, sizeof(str)) == 0 )
        fprintf(file, "\tbroadcast %s\n", str);

    if ( getIpGateway(ifr, str, sizeof(str)) == 0 )
        fprintf(file, "\tgateway %s\n", str);

    return 0;
}

static int saveInterfaceIpConfigAuto(FILE *file, const struct ifreq *ifr)
{
    fprintf(file, "iface %s inet dhcp\n", ifr->ifr_name);

    return 0;
}

static int saveFrom(FILE *from, FILE *to, const char *ifname)
{
#define BUFFLEN     1024
    char    buff[BUFFLEN];
    int     found = 0;
    fpos_t  pos;

    while ( fgets(buff, sizeof(buff), from) )
    {
        if ( found )
        {
            /* Search for a new interface
             */
            if ( strstr(buff, "iface") || strstr(buff, "mapping") )
            {
                fsetpos(from, &pos);
                break;
            }
        }
        else if ( (strstr(buff, "iface") || strstr(buff, "mapping")) &&
                    strstr(buff, ifname) )
        {
            found++;
        }
        else
        {
            /* Write the buff
             */
            fprintf(to, "%s", buff);
        }

        fgetpos(from, &pos);
    }

    return 0;
#undef BUFFLEN
}

int saveInterfaceIpConfig(const struct ifreq *ifr, int isDhcp)
{
#define BUFFLEN     1024
    char    buff[BUFFLEN];
    FILE    *file = NULL;
    FILE    *original = NULL;
    int     ret;

    if ( ifr == NULL )
        return -1;

    if ( (file = fopen(tmpInterface, "w")) == NULL )
        return -1;

    /* Write the other interfaces if there is already a file
     */
    if ( (original = fopen(interface, "r")) != NULL )
    {
        if ( saveFrom(original, file, ifr->ifr_name) )
        {
            fclose(original);
            fclose(file);
            return -1;
        }
    }

    switch ( isDhcp )
    {
        case 0:
        ret = saveInterfaceIpConfigManual(file, ifr);
        break;

        case 1:
        ret = saveInterfaceIpConfigAuto(file, ifr);
        break;

        default:
        ret = -1;
        break;
    }

    /* Write the end of original
     */
    if ( original )
    {
        while ( fgets(buff, sizeof(buff), original) )
            fprintf(file, "%s", buff);

        fclose(original);
    }

    fflush(file);
    fclose(file);

    if ( ret == 0 && (ret = rename(tmpInterface, interface)) )
        perror("rename");

    return ret;
#undef BUFFLEN
}

int getDomainNameServer(char *dest, size_t len)
{
#define BUFFLEN 256
    FILE    *file = NULL;
    char    buff[BUFFLEN];
    char    *ns;
    int     nsLen;

    if ( (file = fopen(resolv, "r")) == NULL )
        return -1;

    while ( fgets(buff, BUFFLEN, file) != NULL )
    {
        /* Ignore comments or empty line
         */
        if ( !*buff || *buff == '#' )
            continue;

        if ( (ns = strstr(buff, "nameserver")) )
        {
            ns += sizeof("nameserver");
            if ( (nsLen = strlen(ns)) && ns[nsLen - 1] == '\n' )
                ns[--nsLen] = '\0';

            if ( (ns = trimSpace(ns)) )
            {
                snprintf(dest, len, "%s", ns);
            }
        }
    }

    fclose(file);

    return 0;
#undef BUFFLEN
}

int setDomainNameServer(const char *ns)
{
    FILE            *file = NULL;
    struct in_addr  in;
    int             ret;

    if ( inet_aton(ns, &in) != 1 )
    {
        perror("inet_aton");
        return -1;
    }

    if ( (file = fopen(tmpResolv, "w")) == NULL )
    {
        perror("fopen");
        return -1;
    }

    fprintf(file, "nameserver\t%s\n", ns);

    fflush(file);
    fclose(file);

    if ( (ret = rename(tmpResolv, resolv)) )
        perror("rename");

    return ret;
}

int foreachInterface(int domain, interface_callback_t cb, void *user)
{
    int                 ret;
    const char          *ptr;
    const struct ifreq  *ifr;

    if ( !init )
        return -1;

    switch ( domain )
    {
        case AF_INET:
        case AF_INET6:
        break;

        default:
        return -1;
    }

    for ( ptr = ifconf.ifc_buf ; ptr < ifconf.ifc_len + ifconf.ifc_buf ; ptr += sizeof(struct ifreq) )
    {
        ifr = (const struct ifreq *) ptr;

        if ( ifr->ifr_addr.sa_family != domain )
            continue;

        if ( cb && (ret = cb(ifr, user)) )
            return ret;
    }

    return 0;
}

int setInterfaceDhcp(const struct ifreq *ifr)
{
    if ( ifr == NULL )
        return -1;

    return getDhcpLease(ifr->ifr_name);
}
