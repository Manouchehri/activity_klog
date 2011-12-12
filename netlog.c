#include <linux/types.h>
#include <linux/module.h>
#include <linux/kprobes.h>
#include <net/sock.h>
#include <linux/in.h>
#include <linux/socket.h>
#include <linux/kallsyms.h>
#include <net/inet_common.h>
#include <net/inet_sock.h>
#include <linux/tcp.h>
#include <net/tcp.h>
#include <linux/unistd.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define CONNECT_PROBE_FAILED -1
#define ACCEPT_PROBE_FAILED -2
#define BIND_PROBE_FAILED -3

#define PROBE_UDP 1

#define MAX_ACTIVE 100

char *get_remote_ip(int);
char *get_local_ip(int);
char *inet_ntoa(struct in_addr in);

static struct socket *socket_hash[PID_MAX_LIMIT];

static int my_inet_stream_connect(struct socket *sock, struct sockaddr *addr, int addr_len, int flags)
{	
	socket_hash[current->pid] = sock;

	jprobe_return();
	return 0;
}

static int post_connect(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	struct inet_sock *inet;
	struct socket *sock = socket_hash[current->pid];
	
	if(sock == NULL || sock->sk == NULL)
	{
		return 0;
	}
	
	if(sock->sk->sk_protocol != IPPROTO_TCP)
	{
		return 0;
	}

	inet = inet_sk(sock->sk);
	
	if(inet == NULL)
	{
		return 0;
	}
	
	printk("netlog: %s[%d] TCP connect %s:%d -> %s:%d (uid=%d)\n", current->comm, current->pid, 
				get_local_ip(inet->inet_saddr), ntohs(inet->inet_sport),
				get_remote_ip(inet->inet_daddr), ntohs(inet->inet_dport), 
				sock_i_uid(sock->sk));	
	
	return 0;
}


/* Post handler probe for accept system call */

static int post_accept(struct kretprobe_instance *ri, struct pt_regs *regs)
{
	int err;
	struct inet_sock *inet;
	struct socket *sock = sockfd_lookup(regs_return_value(regs), &err);
		
	if(sock == NULL || sock->sk == NULL)
	{
		return 0;
	}
	
	if(sock->sk->sk_protocol != IPPROTO_TCP)
	{
		return 0;
	}

	inet = inet_sk(sock->sk);
	
	if(inet == NULL)
	{
		return 0;
	}
	
	printk("netlog: %s[%d] TCP accept %s:%d <- %s:%d (uid=%d)\n", current->comm, current->pid, 
				get_local_ip(inet->inet_saddr), ntohs(inet->inet_sport),
				get_remote_ip(inet->inet_daddr), ntohs(inet->inet_dport), 
				sock_i_uid(sock->sk));	

        return 0;
}


/* UDP protocol is connectionless protocol, so we probe the bind system call */

#if PROBE_UDP
static int my_sys_bind(int sockfd, const struct sockaddr *addr, int addrlen)
{
	int err;
	struct socket * sock;

	sock = sockfd_lookup(sockfd, &err);

	if(sock->sk->sk_protocol == IPPROTO_UDP)
	{
		if(sock->ops->family == PF_INET)
		{
			struct sockaddr_in *addrin = (struct sockaddr_in *)addr;
			char *ip = inet_ntoa(addrin->sin_addr);
			
			if(!strcmp(ip, "0.0.0.0"))
			{
				printk("netlog: %s[%d] accepts UDP at port %d (uid=%d)\n", 
				current->comm, current->pid, ntohs(addrin->sin_port), sock_i_uid(sock->sk));
			}
			else
			{
				printk("netlog: %s[%d] UDP connect to %s:%d by (uid=%d)\n", current->comm,
					current->pid, ip, ntohs(addrin->sin_port), sock_i_uid(sock->sk));
			}
		}
		else if(sock->ops->family == PF_INET6)
		{
			//TODO ipv6 handling by calling the ipv6 version of inet_ntoa
		}
	}
	
	jprobe_return();
	return 0;
}
#endif

/*************************************/
/*         probe definitions        */
/*************************************/

static struct jprobe connect_jprobe = {	
	.entry 			= my_inet_stream_connect,
	.kp = {
		.symbol_name 	= "inet_stream_connect",
	},
};

static struct kretprobe connect_kretprobe = {
        .handler                = post_connect,
        .maxactive              = MAX_ACTIVE,
        .kp = {
        	.symbol_name = "inet_stream_connect"
        	},
};


static struct kretprobe accept_kretprobe = {
        .handler                = post_accept,
        .maxactive              = MAX_ACTIVE,
        .kp = {
        	.symbol_name = "sys_accept4"
        	},
};

#if PROBE_UDP
static struct jprobe bind_jprobe = {	
	.entry 			= my_sys_bind,
	.kp = {
		.symbol_name 	= "sys_bind",
	},
};
#endif

/************************************/
/*             INIT MODULE          */
/************************************/

int init_module(void)
{
	int return_value;
	

	return_value = register_jprobe(&connect_jprobe);

	if(return_value < 0)
	{
		return CONNECT_PROBE_FAILED;
	}

	return_value = register_kretprobe(&connect_kretprobe);
        
        if(return_value < 0) 
        {
                return CONNECT_PROBE_FAILED;
        }

	return_value = register_kretprobe(&accept_kretprobe);
        
        if(return_value < 0) 
        {
                return ACCEPT_PROBE_FAILED;
        }
	
#if PROBE_UDP
	return_value = register_jprobe(&bind_jprobe);

	if(return_value < 0)
	{
		return BIND_PROBE_FAILED;
	}
#endif	
	
	printk("netlog: planted\n");        

	return 0;
}

/************************************/
/*             EXIT MODULE          */
/************************************/

void cleanup_module(void)
{
  	unregister_jprobe(&connect_jprobe);
	unregister_kretprobe(&connect_kretprobe);
	unregister_kretprobe(&accept_kretprobe);
#if PROBE_UDP
  	unregister_jprobe(&bind_jprobe);
#endif
	
	printk("netlog: unplanted\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Panos Sakkos <panos.sakkos@cern.ch>");
MODULE_DESCRIPTION("TODO");

char *inet_ntoa(struct in_addr in)
{
	static char b[18];
	register char *p;

	p = (char *)&in;
#define	UC(b)	(((int)b)&0xff)
	(void)snprintf(b, sizeof(b),
	    "%u.%u.%u.%u", UC(p[0]), UC(p[1]), UC(p[2]), UC(p[3]));
	return (b);
}

char *get_local_ip(int in)
{
	static char b[18];
	register char*p;
	
	p = (char *)&in;
#define	UC(b)	(((int)b)&0xff)
	(void)snprintf(b, sizeof(b),
	    "%u.%u.%u.%u", UC(p[0]), UC(p[1]), UC(p[2]), UC(p[3]));
	return (b);		
}

char *get_remote_ip(int in)
{
	static char b[18];
	register char*p;
	
	p = (char *)&in;
#define	UC(b)	(((int)b)&0xff)
	(void)snprintf(b, sizeof(b),
	    "%u.%u.%u.%u", UC(p[0]), UC(p[1]), UC(p[2]), UC(p[3]));
	return (b);		
}

