/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Netlink does not exist on Darwin; the types let code that names it compile, and sockets of it fail. */
#ifndef HUSK_COMPAT_RTNETLINK_H
#define HUSK_COMPAT_RTNETLINK_H
#include <stdint.h>
#include <sys/socket.h>
#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif
#define NETLINK_ROUTE 0
#define NETLINK_INET_DIAG 4
typedef unsigned short __kernel_sa_family_t;
struct sockaddr_nl { __kernel_sa_family_t nl_family; unsigned short nl_pad; uint32_t nl_pid; uint32_t nl_groups; };
struct nlmsghdr { uint32_t nlmsg_len; uint16_t nlmsg_type; uint16_t nlmsg_flags; uint32_t nlmsg_seq; uint32_t nlmsg_pid; };
#endif
