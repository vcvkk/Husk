/* SPDX-License-Identifier: GPL-2.0-or-later */
/* net/if_arp.h, in the macOS SDK and not the iPhone's: OsConstants reads ARPHRD_ETHER and
 * ARPHRD_LOOPBACK from it, which compat/linux_constants.h defines with Linux's values. */
#include "linux_constants.h"
