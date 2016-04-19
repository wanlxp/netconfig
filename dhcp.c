#include "dhcp.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>


#define LEASES      "/var/lib/dhcp/dhclient.leases"
#define DHCLIENT    "/sbin/dhclient"


int isInterfaceDynamic(const char *ifname)
{
#define BUFFLEN     1024
    char    buff[BUFFLEN];
    FILE    *f;
    int     ret = 0;

    if ( ifname == NULL ||
            (f = fopen(LEASES, "r")) == NULL )
        return 0;

    while ( fgets(buff, sizeof(buff), f) )
    {
        if ( strstr(buff, "interface") && strstr(buff, ifname) )
        {
            ret++;
            break;
        }
    }

    fclose(f);

    return ret;
#undef BUFFLEN
}

int getDhcpLease(const char *ifname)
{
    int         ret;
    pid_t       pid;
    const char  *arg [] = {DHCLIENT, ifname, NULL};

    if ( ifname == NULL )
        return 0;

    if ( (pid = fork()) == -1 )
    {
        perror("fork");
        return -1;
    }
    if ( pid == 0 )
    {
        /* Child should never return
         */
        execv(arg[0], (char * const *) arg);
        ret = -1;
    }
    else
    {
        /* Father just wait the child for its retcode
         */
        waitpid(pid, &ret, 0);

        if ( WIFEXITED(ret) )
            ret = 0;
        else
            ret = -1;
    }

    return ret;
}
