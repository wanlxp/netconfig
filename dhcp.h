#ifndef __DHCP_H__
#define __DHCP_H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int isInterfaceDynamic(const char *ifname);

int getDhcpLease(const char *ifname);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __DHCP_H__ */
