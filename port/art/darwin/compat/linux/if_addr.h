/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Interface address flags (Linux values), which libcore exposes as OsConstants. */
#ifndef HUSK_COMPAT_IF_ADDR_H
#define HUSK_COMPAT_IF_ADDR_H
#define IFA_F_SECONDARY 0x01
#define IFA_F_TEMPORARY IFA_F_SECONDARY
#define IFA_F_NODAD 0x02
#define IFA_F_OPTIMISTIC 0x04
#define IFA_F_DADFAILED 0x08
#define IFA_F_HOMEADDRESS 0x10
#define IFA_F_DEPRECATED 0x20
#define IFA_F_TENTATIVE 0x40
#define IFA_F_PERMANENT 0x80
#endif
