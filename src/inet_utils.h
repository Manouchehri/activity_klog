#ifndef __NETLOG_IPUTILS__
#define __NETLOG_IPUTILS__

/* API that provides destination and source ip addresses,
 * given the target struct sock, struct socket or
 *the struct sockaddr.
 */

#include <linux/net.h>
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 25)
        #define SADDR saddr
        #define DADDR daddr
        #define SPORT sport
        #define DPORT dport
#else
        #define SADDR inet_saddr
        #define DADDR inet_daddr
        #define SPORT inet_sport
        #define DPORT inet_dport
#endif

void copy_ip(void *dst, const void *src, unsigned short family);

#endif /* __NETLOG_IPUTILS__ */
