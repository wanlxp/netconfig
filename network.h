#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int networkInit(void);

void networkClean(void);

typedef int (*interface_callback_t)(const struct ifreq *ifr, void *user);

int foreachInterface(int domain, interface_callback_t cb, void *user);
#define foreachInterfaceIpv4(cb, user)  foreachInterface(AF_INET, cb, user)
#define foreachInterfaceIpv6(cb, user)  foreachInterface(AF_INET6, cb, user)

const struct ifreq *getInterfaceByName(const char *ifname, int domain);
#define getInterfaceByNameIpv4(ifname)  getInterfaceByName(ifname, AF_INET)
#define getInterfaceByNameIpv6(ifname)  getInterfaceByName(ifname, AF_INET6)

int addAllInterfaces(void);

int isInterfacePlugged(const struct ifreq *ifr);

int getIpAddress(const struct ifreq *ifr, char *dest, size_t len);

int setInterfaceIpAddress(const struct ifreq *ifr, const char *ip);

int getMacAddress(const struct ifreq *ifr, char *dest, size_t len);

int setInterfaceMacAddress(const struct ifreq *ifr, const char *mac);

int getIpMask(const struct ifreq *ifr, char *dest, size_t len);

int setInterfaceIpMask(const struct ifreq *ifr, const char *mask);

int getIpBroadcast(const struct ifreq *ifr, char *dest, size_t len);

int setInterfaceIpBroadcast(const struct ifreq *ifr, const char *bcast);

int getIpGateway(const struct ifreq *ifr, char *dest, size_t len);

int setInterfaceIpGateway(const struct ifreq *ifr, const char *gw);

#define MANUAL  0
#define AUTO    1
int saveInterfaceIpConfig(const struct ifreq *ifr, int isDhcp);

int setInterfaceDhcp(const struct ifreq *ifr);

int getDomainNameServer(char *dest, size_t len);

int setDomainNameServer(const char *ns);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __NETWORK_H__ */
