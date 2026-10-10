/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Packet sockets do not exist on Darwin; the type lets code that names it compile. */
#ifndef HUSK_COMPAT_PACKET_H
#define HUSK_COMPAT_PACKET_H
#include <stdint.h>
#ifndef AF_PACKET
#define AF_PACKET 17
#endif
struct sockaddr_ll { unsigned short sll_family; uint16_t sll_protocol; int sll_ifindex; unsigned short sll_hatype;
                     unsigned char sll_pkttype; unsigned char sll_halen; unsigned char sll_addr[8]; };
#endif
