/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Every constant android.system.OsConstants reads without checking that it exists, for
   those Darwin lacks: Linux's value, or for an errno, one Darwin never returns. Generated
   from Linux's headers; included by OsConstants.cpp after its own includes. */
#ifndef HUSK_COMPAT_LINUX_CONSTANTS_H
#define HUSK_COMPAT_LINUX_CONSTANTS_H
#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 10
#endif
#ifndef AF_PACKET
#define AF_PACKET 17
#endif
#ifndef AF_NETLINK
#define AF_NETLINK 16
#endif
#ifndef AF_UNIX
#define AF_UNIX 1
#endif
#ifndef AF_UNSPEC
#define AF_UNSPEC 0
#endif
#ifndef AI_ADDRCONFIG
#define AI_ADDRCONFIG 32
#endif
#ifndef AI_ALL
#define AI_ALL 16
#endif
#ifndef AI_CANONNAME
#define AI_CANONNAME 2
#endif
#ifndef AI_NUMERICHOST
#define AI_NUMERICHOST 4
#endif
#ifndef AI_PASSIVE
#define AI_PASSIVE 1
#endif
#ifndef AI_V4MAPPED
#define AI_V4MAPPED 8
#endif
#ifndef ARPHRD_ETHER
#define ARPHRD_ETHER 1
#endif
#ifndef ARPHRD_LOOPBACK
#define ARPHRD_LOOPBACK 772
#endif
#ifndef CAP_AUDIT_CONTROL
#define CAP_AUDIT_CONTROL 30
#endif
#ifndef CAP_AUDIT_WRITE
#define CAP_AUDIT_WRITE 29
#endif
#ifndef CAP_BLOCK_SUSPEND
#define CAP_BLOCK_SUSPEND 36
#endif
#ifndef CAP_CHOWN
#define CAP_CHOWN 0
#endif
#ifndef CAP_DAC_OVERRIDE
#define CAP_DAC_OVERRIDE 1
#endif
#ifndef CAP_DAC_READ_SEARCH
#define CAP_DAC_READ_SEARCH 2
#endif
#ifndef CAP_FOWNER
#define CAP_FOWNER 3
#endif
#ifndef CAP_FSETID
#define CAP_FSETID 4
#endif
#ifndef CAP_IPC_LOCK
#define CAP_IPC_LOCK 14
#endif
#ifndef CAP_IPC_OWNER
#define CAP_IPC_OWNER 15
#endif
#ifndef CAP_KILL
#define CAP_KILL 5
#endif
#ifndef CAP_LEASE
#define CAP_LEASE 28
#endif
#ifndef CAP_LINUX_IMMUTABLE
#define CAP_LINUX_IMMUTABLE 9
#endif
#ifndef CAP_MAC_ADMIN
#define CAP_MAC_ADMIN 33
#endif
#ifndef CAP_MAC_OVERRIDE
#define CAP_MAC_OVERRIDE 32
#endif
#ifndef CAP_MKNOD
#define CAP_MKNOD 27
#endif
#ifndef CAP_NET_ADMIN
#define CAP_NET_ADMIN 12
#endif
#ifndef CAP_NET_BIND_SERVICE
#define CAP_NET_BIND_SERVICE 10
#endif
#ifndef CAP_NET_BROADCAST
#define CAP_NET_BROADCAST 11
#endif
#ifndef CAP_NET_RAW
#define CAP_NET_RAW 13
#endif
#ifndef CAP_SETFCAP
#define CAP_SETFCAP 31
#endif
#ifndef CAP_SETGID
#define CAP_SETGID 6
#endif
#ifndef CAP_SETPCAP
#define CAP_SETPCAP 8
#endif
#ifndef CAP_SETUID
#define CAP_SETUID 7
#endif
#ifndef CAP_SYS_ADMIN
#define CAP_SYS_ADMIN 21
#endif
#ifndef CAP_SYS_BOOT
#define CAP_SYS_BOOT 22
#endif
#ifndef CAP_SYS_CHROOT
#define CAP_SYS_CHROOT 18
#endif
#ifndef CAP_SYSLOG
#define CAP_SYSLOG 34
#endif
#ifndef CAP_SYS_MODULE
#define CAP_SYS_MODULE 16
#endif
#ifndef CAP_SYS_NICE
#define CAP_SYS_NICE 23
#endif
#ifndef CAP_SYS_PACCT
#define CAP_SYS_PACCT 20
#endif
#ifndef CAP_SYS_PTRACE
#define CAP_SYS_PTRACE 19
#endif
#ifndef CAP_SYS_RAWIO
#define CAP_SYS_RAWIO 17
#endif
#ifndef CAP_SYS_RESOURCE
#define CAP_SYS_RESOURCE 24
#endif
#ifndef CAP_SYS_TIME
#define CAP_SYS_TIME 25
#endif
#ifndef CAP_SYS_TTY_CONFIG
#define CAP_SYS_TTY_CONFIG 26
#endif
#ifndef CAP_WAKE_ALARM
#define CAP_WAKE_ALARM 35
#endif
#ifndef E2BIG
#define E2BIG (1000 + 7)
#endif
#ifndef EACCES
#define EACCES (1000 + 13)
#endif
#ifndef EADDRINUSE
#define EADDRINUSE (1000 + 98)
#endif
#ifndef EADDRNOTAVAIL
#define EADDRNOTAVAIL (1000 + 99)
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT (1000 + 97)
#endif
#ifndef EAGAIN
#define EAGAIN (1000 + 11)
#endif
#ifndef EAI_AGAIN
#define EAI_AGAIN -3
#endif
#ifndef EAI_BADFLAGS
#define EAI_BADFLAGS -1
#endif
#ifndef EAI_FAIL
#define EAI_FAIL -4
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY -6
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY -10
#endif
#ifndef EAI_NODATA
#define EAI_NODATA -5
#endif
#ifndef EAI_NONAME
#define EAI_NONAME -2
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE -8
#endif
#ifndef EAI_SOCKTYPE
#define EAI_SOCKTYPE -7
#endif
#ifndef EAI_SYSTEM
#define EAI_SYSTEM -11
#endif
#ifndef EALREADY
#define EALREADY (1000 + 114)
#endif
#ifndef EBADF
#define EBADF (1000 + 9)
#endif
#ifndef EBADMSG
#define EBADMSG (1000 + 74)
#endif
#ifndef EBUSY
#define EBUSY (1000 + 16)
#endif
#ifndef ECANCELED
#define ECANCELED (1000 + 125)
#endif
#ifndef ECHILD
#define ECHILD (1000 + 10)
#endif
#ifndef ECONNABORTED
#define ECONNABORTED (1000 + 103)
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED (1000 + 111)
#endif
#ifndef ECONNRESET
#define ECONNRESET (1000 + 104)
#endif
#ifndef EDEADLK
#define EDEADLK 35
#endif
#ifndef EDESTADDRREQ
#define EDESTADDRREQ (1000 + 89)
#endif
#ifndef EDOM
#define EDOM (1000 + 33)
#endif
#ifndef EDQUOT
#define EDQUOT (1000 + 122)
#endif
#ifndef EEXIST
#define EEXIST (1000 + 17)
#endif
#ifndef EFAULT
#define EFAULT (1000 + 14)
#endif
#ifndef EFBIG
#define EFBIG (1000 + 27)
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH (1000 + 113)
#endif
#ifndef EIDRM
#define EIDRM (1000 + 43)
#endif
#ifndef EILSEQ
#define EILSEQ (1000 + 84)
#endif
#ifndef EINPROGRESS
#define EINPROGRESS (1000 + 115)
#endif
#ifndef EINTR
#define EINTR (1000 + 4)
#endif
#ifndef EINVAL
#define EINVAL (1000 + 22)
#endif
#ifndef EIO
#define EIO (1000 + 5)
#endif
#ifndef EISCONN
#define EISCONN (1000 + 106)
#endif
#ifndef EISDIR
#define EISDIR (1000 + 21)
#endif
#ifndef ELOOP
#define ELOOP (1000 + 40)
#endif
#ifndef EMFILE
#define EMFILE (1000 + 24)
#endif
#ifndef EMLINK
#define EMLINK (1000 + 31)
#endif
#ifndef EMSGSIZE
#define EMSGSIZE (1000 + 90)
#endif
#ifndef EMULTIHOP
#define EMULTIHOP (1000 + 72)
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG (1000 + 36)
#endif
#ifndef ENETDOWN
#define ENETDOWN (1000 + 100)
#endif
#ifndef ENETRESET
#define ENETRESET (1000 + 102)
#endif
#ifndef ENETUNREACH
#define ENETUNREACH (1000 + 101)
#endif
#ifndef ENFILE
#define ENFILE (1000 + 23)
#endif
#ifndef ENOBUFS
#define ENOBUFS (1000 + 105)
#endif
#ifndef ENODATA
#define ENODATA (1000 + 61)
#endif
#ifndef ENODEV
#define ENODEV (1000 + 19)
#endif
#ifndef ENOENT
#define ENOENT (1000 + 2)
#endif
#ifndef ENOEXEC
#define ENOEXEC (1000 + 8)
#endif
#ifndef ENOLCK
#define ENOLCK (1000 + 37)
#endif
#ifndef ENOLINK
#define ENOLINK (1000 + 67)
#endif
#ifndef ENOMEM
#define ENOMEM (1000 + 12)
#endif
#ifndef ENOMSG
#define ENOMSG (1000 + 42)
#endif
#ifndef ENONET
#define ENONET (1000 + 64)
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT (1000 + 92)
#endif
#ifndef ENOSPC
#define ENOSPC (1000 + 28)
#endif
#ifndef ENOSR
#define ENOSR (1000 + 63)
#endif
#ifndef ENOSTR
#define ENOSTR (1000 + 60)
#endif
#ifndef ENOSYS
#define ENOSYS (1000 + 38)
#endif
#ifndef ENOTCONN
#define ENOTCONN (1000 + 107)
#endif
#ifndef ENOTDIR
#define ENOTDIR (1000 + 20)
#endif
#ifndef ENOTEMPTY
#define ENOTEMPTY (1000 + 39)
#endif
#ifndef ENOTSOCK
#define ENOTSOCK (1000 + 88)
#endif
#ifndef ENOTSUP
#define ENOTSUP (1000 + 95)
#endif
#ifndef ENOTTY
#define ENOTTY (1000 + 25)
#endif
#ifndef ENXIO
#define ENXIO (1000 + 6)
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP 95
#endif
#ifndef EOVERFLOW
#define EOVERFLOW (1000 + 75)
#endif
#ifndef EPERM
#define EPERM (1000 + 1)
#endif
#ifndef EPIPE
#define EPIPE (1000 + 32)
#endif
#ifndef EPROTO
#define EPROTO (1000 + 71)
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT (1000 + 93)
#endif
#ifndef EPROTOTYPE
#define EPROTOTYPE (1000 + 91)
#endif
#ifndef ERANGE
#define ERANGE (1000 + 34)
#endif
#ifndef EROFS
#define EROFS (1000 + 30)
#endif
#ifndef ESPIPE
#define ESPIPE (1000 + 29)
#endif
#ifndef ESRCH
#define ESRCH (1000 + 3)
#endif
#ifndef ESTALE
#define ESTALE (1000 + 116)
#endif
#ifndef ETH_P_ALL
#define ETH_P_ALL 3
#endif
#ifndef ETH_P_ARP
#define ETH_P_ARP 2054
#endif
#ifndef ETH_P_IP
#define ETH_P_IP 2048
#endif
#ifndef ETH_P_IPV6
#define ETH_P_IPV6 34525
#endif
#ifndef ETIME
#define ETIME (1000 + 62)
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT (1000 + 110)
#endif
#ifndef ETXTBSY
#define ETXTBSY (1000 + 26)
#endif
#ifndef EUSERS
#define EUSERS (1000 + 87)
#endif
#ifndef EXDEV
#define EXDEV (1000 + 18)
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif
#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC 1
#endif
#ifndef FIONREAD
#define FIONREAD 21531
#endif
#ifndef F_DUPFD
#define F_DUPFD 0
#endif
#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC 1030
#endif
#ifndef F_GETFD
#define F_GETFD 1
#endif
#ifndef F_GETFL
#define F_GETFL 3
#endif
#ifndef F_GETLK
#define F_GETLK 5
#endif
#ifndef F_GETOWN
#define F_GETOWN 9
#endif
#ifndef F_OK
#define F_OK 0
#endif
#ifndef F_RDLCK
#define F_RDLCK 0
#endif
#ifndef F_SETFD
#define F_SETFD 2
#endif
#ifndef F_SETFL
#define F_SETFL 4
#endif
#ifndef F_SETLK
#define F_SETLK 6
#endif
#ifndef F_SETLKW
#define F_SETLKW 7
#endif
#ifndef F_SETOWN
#define F_SETOWN 8
#endif
#ifndef F_UNLCK
#define F_UNLCK 2
#endif
#ifndef F_WRLCK
#define F_WRLCK 1
#endif
#ifndef ICMP_ECHO
#define ICMP_ECHO 8
#endif
#ifndef ICMP_ECHOREPLY
#define ICMP_ECHOREPLY 0
#endif
#ifndef ICMP6_ECHO_REQUEST
#define ICMP6_ECHO_REQUEST 128
#endif
#ifndef ICMP6_ECHO_REPLY
#define ICMP6_ECHO_REPLY 129
#endif
#ifndef IFF_ALLMULTI
#define IFF_ALLMULTI 512
#endif
#ifndef IFF_BROADCAST
#define IFF_BROADCAST 2
#endif
#ifndef IFF_DEBUG
#define IFF_DEBUG 4
#endif
#ifndef IFF_LOOPBACK
#define IFF_LOOPBACK 8
#endif
#ifndef IFF_MULTICAST
#define IFF_MULTICAST 4096
#endif
#ifndef IFF_NOARP
#define IFF_NOARP 128
#endif
#ifndef IFF_NOTRAILERS
#define IFF_NOTRAILERS 32
#endif
#ifndef IFF_POINTOPOINT
#define IFF_POINTOPOINT 16
#endif
#ifndef IFF_PROMISC
#define IFF_PROMISC 256
#endif
#ifndef IFF_RUNNING
#define IFF_RUNNING 64
#endif
#ifndef IFF_UP
#define IFF_UP 1
#endif
#ifndef IPPROTO_ICMP
#define IPPROTO_ICMP 1
#endif
#ifndef IPPROTO_ICMPV6
#define IPPROTO_ICMPV6 58
#endif
#ifndef IPPROTO_IP
#define IPPROTO_IP 0
#endif
#ifndef IPPROTO_IPV6
#define IPPROTO_IPV6 41
#endif
#ifndef IPPROTO_RAW
#define IPPROTO_RAW 255
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef IPPROTO_UDP
#define IPPROTO_UDP 17
#endif
#ifndef IPV6_CHECKSUM
#define IPV6_CHECKSUM 7
#endif
#ifndef IPV6_MULTICAST_HOPS
#define IPV6_MULTICAST_HOPS 18
#endif
#ifndef IPV6_MULTICAST_IF
#define IPV6_MULTICAST_IF 17
#endif
#ifndef IPV6_MULTICAST_LOOP
#define IPV6_MULTICAST_LOOP 19
#endif
#ifndef IPV6_UNICAST_HOPS
#define IPV6_UNICAST_HOPS 16
#endif
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 26
#endif
#ifndef IP_MULTICAST_ALL
#define IP_MULTICAST_ALL 49
#endif
#ifndef IP_MULTICAST_IF
#define IP_MULTICAST_IF 32
#endif
#ifndef IP_MULTICAST_LOOP
#define IP_MULTICAST_LOOP 34
#endif
#ifndef IP_MULTICAST_TTL
#define IP_MULTICAST_TTL 33
#endif
#ifndef IP_RECVTOS
#define IP_RECVTOS 13
#endif
#ifndef IP_TOS
#define IP_TOS 1
#endif
#ifndef IP_TTL
#define IP_TTL 2
#endif
#ifndef MAP_FIXED
#define MAP_FIXED 16
#endif
#ifndef MAP_POPULATE
#define MAP_POPULATE 32768
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE 2
#endif
#ifndef MAP_SHARED
#define MAP_SHARED 1
#endif
#ifndef MCL_CURRENT
#define MCL_CURRENT 1
#endif
#ifndef MCL_FUTURE
#define MCL_FUTURE 2
#endif
#ifndef MSG_CTRUNC
#define MSG_CTRUNC 8
#endif
#ifndef MSG_DONTROUTE
#define MSG_DONTROUTE 4
#endif
#ifndef MSG_EOR
#define MSG_EOR 128
#endif
#ifndef MSG_OOB
#define MSG_OOB 1
#endif
#ifndef MSG_PEEK
#define MSG_PEEK 2
#endif
#ifndef MSG_TRUNC
#define MSG_TRUNC 32
#endif
#ifndef MSG_WAITALL
#define MSG_WAITALL 256
#endif
#ifndef MS_ASYNC
#define MS_ASYNC 1
#endif
#ifndef MS_INVALIDATE
#define MS_INVALIDATE 2
#endif
#ifndef MS_SYNC
#define MS_SYNC 4
#endif
#ifndef NETLINK_NETFILTER
#define NETLINK_NETFILTER 12
#endif
#ifndef NETLINK_ROUTE
#define NETLINK_ROUTE 0
#endif
#ifndef NI_DGRAM
#define NI_DGRAM 16
#endif
#ifndef NI_NAMEREQD
#define NI_NAMEREQD 8
#endif
#ifndef NI_NOFQDN
#define NI_NOFQDN 4
#endif
#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#endif
#ifndef NI_NUMERICSERV
#define NI_NUMERICSERV 2
#endif
#ifndef O_ACCMODE
#define O_ACCMODE 3
#endif
#ifndef O_APPEND
#define O_APPEND 1024
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC 524288
#endif
#ifndef O_CREAT
#define O_CREAT 64
#endif
#ifndef O_EXCL
#define O_EXCL 128
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 256
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 131072
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 2048
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif
#ifndef O_RDWR
#define O_RDWR 2
#endif
#ifndef O_SYNC
#define O_SYNC 1052672
#endif
#ifndef O_DSYNC
#define O_DSYNC 4096
#endif
#ifndef O_TRUNC
#define O_TRUNC 512
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef POLLERR
#define POLLERR 8
#endif
#ifndef POLLHUP
#define POLLHUP 16
#endif
#ifndef POLLIN
#define POLLIN 1
#endif
#ifndef POLLNVAL
#define POLLNVAL 32
#endif
#ifndef POLLOUT
#define POLLOUT 4
#endif
#ifndef POLLPRI
#define POLLPRI 2
#endif
#ifndef POLLRDBAND
#define POLLRDBAND 128
#endif
#ifndef POLLRDNORM
#define POLLRDNORM 64
#endif
#ifndef POLLWRBAND
#define POLLWRBAND 512
#endif
#ifndef POLLWRNORM
#define POLLWRNORM 256
#endif
#ifndef PROT_EXEC
#define PROT_EXEC 4
#endif
#ifndef PROT_NONE
#define PROT_NONE 0
#endif
#ifndef PROT_READ
#define PROT_READ 1
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 2
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef RLIMIT_NOFILE
#define RLIMIT_NOFILE 7
#endif
#ifndef RT_SCOPE_HOST
#define RT_SCOPE_HOST 254
#endif
#ifndef RT_SCOPE_LINK
#define RT_SCOPE_LINK 253
#endif
#ifndef RT_SCOPE_NOWHERE
#define RT_SCOPE_NOWHERE 255
#endif
#ifndef RT_SCOPE_SITE
#define RT_SCOPE_SITE 200
#endif
#ifndef RT_SCOPE_UNIVERSE
#define RT_SCOPE_UNIVERSE 0
#endif
#ifndef RTMGRP_IPV4_IFADDR
#define RTMGRP_IPV4_IFADDR 16
#endif
#ifndef RTMGRP_IPV4_MROUTE
#define RTMGRP_IPV4_MROUTE 32
#endif
#ifndef RTMGRP_IPV4_ROUTE
#define RTMGRP_IPV4_ROUTE 64
#endif
#ifndef RTMGRP_IPV4_RULE
#define RTMGRP_IPV4_RULE 128
#endif
#ifndef RTMGRP_IPV6_IFADDR
#define RTMGRP_IPV6_IFADDR 256
#endif
#ifndef RTMGRP_IPV6_IFINFO
#define RTMGRP_IPV6_IFINFO 2048
#endif
#ifndef RTMGRP_IPV6_MROUTE
#define RTMGRP_IPV6_MROUTE 512
#endif
#ifndef RTMGRP_IPV6_PREFIX
#define RTMGRP_IPV6_PREFIX 131072
#endif
#ifndef RTMGRP_IPV6_ROUTE
#define RTMGRP_IPV6_ROUTE 1024
#endif
#ifndef RTMGRP_LINK
#define RTMGRP_LINK 1
#endif
#ifndef RTMGRP_NEIGH
#define RTMGRP_NEIGH 4
#endif
#ifndef RTMGRP_NOTIFY
#define RTMGRP_NOTIFY 2
#endif
#ifndef RTMGRP_TC
#define RTMGRP_TC 8
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif
#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SHUT_RD
#define SHUT_RD 0
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR 2
#endif
#ifndef SHUT_WR
#define SHUT_WR 1
#endif
#ifndef SIGABRT
#define SIGABRT 6
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGBUS
#define SIGBUS 7
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#ifndef SIGCONT
#define SIGCONT 18
#endif
#ifndef SIGFPE
#define SIGFPE 8
#endif
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGILL
#define SIGILL 4
#endif
#ifndef SIGINT
#define SIGINT 2
#endif
#ifndef SIGIO
#define SIGIO 29
#endif
#ifndef SIGKILL
#define SIGKILL 9
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGPROF
#define SIGPROF 27
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGSEGV
#define SIGSEGV 11
#endif
#ifndef SIGSTOP
#define SIGSTOP 19
#endif
#ifndef SIGSYS
#define SIGSYS 31
#endif
#ifndef SIGTERM
#define SIGTERM 15
#endif
#ifndef SIGTRAP
#define SIGTRAP 5
#endif
#ifndef SIGTSTP
#define SIGTSTP 20
#endif
#ifndef SIGTTIN
#define SIGTTIN 21
#endif
#ifndef SIGTTOU
#define SIGTTOU 22
#endif
#ifndef SIGURG
#define SIGURG 23
#endif
#ifndef SIGUSR1
#define SIGUSR1 10
#endif
#ifndef SIGUSR2
#define SIGUSR2 12
#endif
#ifndef SIGVTALRM
#define SIGVTALRM 26
#endif
#ifndef SIGWINCH
#define SIGWINCH 28
#endif
#ifndef SIGXCPU
#define SIGXCPU 24
#endif
#ifndef SIGXFSZ
#define SIGXFSZ 25
#endif
#ifndef SIOCGIFADDR
#define SIOCGIFADDR 35093
#endif
#ifndef SIOCGIFBRDADDR
#define SIOCGIFBRDADDR 35097
#endif
#ifndef SIOCGIFDSTADDR
#define SIOCGIFDSTADDR 35095
#endif
#ifndef SIOCGIFNETMASK
#define SIOCGIFNETMASK 35099
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOCK_RAW
#define SOCK_RAW 3
#endif
#ifndef SOCK_SEQPACKET
#define SOCK_SEQPACKET 5
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_BROADCAST
#define SO_BROADCAST 6
#endif
#ifndef SO_DEBUG
#define SO_DEBUG 1
#endif
#ifndef SO_DONTROUTE
#define SO_DONTROUTE 5
#endif
#ifndef SO_ERROR
#define SO_ERROR 4
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 9
#endif
#ifndef SO_LINGER
#define SO_LINGER 13
#endif
#ifndef SO_OOBINLINE
#define SO_OOBINLINE 10
#endif
#ifndef SO_RCVBUF
#define SO_RCVBUF 8
#endif
#ifndef SO_RCVLOWAT
#define SO_RCVLOWAT 18
#endif
#ifndef SO_RCVTIMEO
#define SO_RCVTIMEO 20
#endif
#ifndef SO_REUSEADDR
#define SO_REUSEADDR 2
#endif
#ifndef SO_SNDBUF
#define SO_SNDBUF 7
#endif
#ifndef SO_SNDLOWAT
#define SO_SNDLOWAT 19
#endif
#ifndef SO_SNDTIMEO
#define SO_SNDTIMEO 21
#endif
#ifndef SO_TYPE
#define SO_TYPE 3
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef ST_MANDLOCK
#define ST_MANDLOCK 64
#endif
#ifndef ST_NOATIME
#define ST_NOATIME 1024
#endif
#ifndef ST_NODEV
#define ST_NODEV 4
#endif
#ifndef ST_NODIRATIME
#define ST_NODIRATIME 2048
#endif
#ifndef ST_NOEXEC
#define ST_NOEXEC 8
#endif
#ifndef ST_NOSUID
#define ST_NOSUID 2
#endif
#ifndef ST_RDONLY
#define ST_RDONLY 1
#endif
#ifndef ST_RELATIME
#define ST_RELATIME 4096
#endif
#ifndef ST_SYNCHRONOUS
#define ST_SYNCHRONOUS 16
#endif
#ifndef S_IFBLK
#define S_IFBLK 24576
#endif
#ifndef S_IFCHR
#define S_IFCHR 8192
#endif
#ifndef S_IFDIR
#define S_IFDIR 16384
#endif
#ifndef S_IFIFO
#define S_IFIFO 4096
#endif
#ifndef S_IFLNK
#define S_IFLNK 40960
#endif
#ifndef S_IFMT
#define S_IFMT 61440
#endif
#ifndef S_IFREG
#define S_IFREG 32768
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 49152
#endif
#ifndef S_IRGRP
#define S_IRGRP 32
#endif
#ifndef S_IROTH
#define S_IROTH 4
#endif
#ifndef S_IRUSR
#define S_IRUSR 256
#endif
#ifndef S_IRWXG
#define S_IRWXG 56
#endif
#ifndef S_IRWXO
#define S_IRWXO 7
#endif
#ifndef S_IRWXU
#define S_IRWXU 448
#endif
#ifndef S_ISGID
#define S_ISGID 1024
#endif
#ifndef S_ISUID
#define S_ISUID 2048
#endif
#ifndef S_ISVTX
#define S_ISVTX 512
#endif
#ifndef S_IWGRP
#define S_IWGRP 16
#endif
#ifndef S_IWOTH
#define S_IWOTH 2
#endif
#ifndef S_IWUSR
#define S_IWUSR 128
#endif
#ifndef S_IXGRP
#define S_IXGRP 8
#endif
#ifndef S_IXOTH
#define S_IXOTH 1
#endif
#ifndef S_IXUSR
#define S_IXUSR 64
#endif
#ifndef TCP_NODELAY
#define TCP_NODELAY 1
#endif
#ifndef TIOCOUTQ
#define TIOCOUTQ 21521
#endif
#ifndef UDP_ENCAP
#define UDP_ENCAP 100
#endif
#ifndef UDP_ENCAP_ESPINUDP_NON_IKE
#define UDP_ENCAP_ESPINUDP_NON_IKE 1
#endif
#ifndef UDP_ENCAP_ESPINUDP
#define UDP_ENCAP_ESPINUDP 2
#endif
#ifndef WCONTINUED
#define WCONTINUED 8
#endif
#ifndef WEXITED
#define WEXITED 4
#endif
#ifndef WNOHANG
#define WNOHANG 1
#endif
#ifndef WNOWAIT
#define WNOWAIT 16777216
#endif
#ifndef WSTOPPED
#define WSTOPPED 2
#endif
#ifndef WUNTRACED
#define WUNTRACED 2
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef XATTR_CREATE
#define XATTR_CREATE 1
#endif
#ifndef XATTR_REPLACE
#define XATTR_REPLACE 2
#endif
#ifndef X_OK
#define X_OK 1
#endif
#ifndef _SC_2_CHAR_TERM
#define _SC_2_CHAR_TERM 95
#endif
#ifndef _SC_2_C_BIND
#define _SC_2_C_BIND 47
#endif
#ifndef _SC_2_C_DEV
#define _SC_2_C_DEV 48
#endif
#ifndef _SC_2_FORT_DEV
#define _SC_2_FORT_DEV 49
#endif
#ifndef _SC_2_FORT_RUN
#define _SC_2_FORT_RUN 50
#endif
#ifndef _SC_2_LOCALEDEF
#define _SC_2_LOCALEDEF 52
#endif
#ifndef _SC_2_SW_DEV
#define _SC_2_SW_DEV 51
#endif
#ifndef _SC_2_UPE
#define _SC_2_UPE 97
#endif
#ifndef _SC_2_VERSION
#define _SC_2_VERSION 46
#endif
#ifndef _SC_AIO_LISTIO_MAX
#define _SC_AIO_LISTIO_MAX 23
#endif
#ifndef _SC_AIO_MAX
#define _SC_AIO_MAX 24
#endif
#ifndef _SC_AIO_PRIO_DELTA_MAX
#define _SC_AIO_PRIO_DELTA_MAX 25
#endif
#ifndef _SC_ARG_MAX
#define _SC_ARG_MAX 0
#endif
#ifndef _SC_ASYNCHRONOUS_IO
#define _SC_ASYNCHRONOUS_IO 12
#endif
#ifndef _SC_ATEXIT_MAX
#define _SC_ATEXIT_MAX 87
#endif
#ifndef _SC_BC_BASE_MAX
#define _SC_BC_BASE_MAX 36
#endif
#ifndef _SC_BC_DIM_MAX
#define _SC_BC_DIM_MAX 37
#endif
#ifndef _SC_BC_SCALE_MAX
#define _SC_BC_SCALE_MAX 38
#endif
#ifndef _SC_BC_STRING_MAX
#define _SC_BC_STRING_MAX 39
#endif
#ifndef _SC_CHILD_MAX
#define _SC_CHILD_MAX 1
#endif
#ifndef _SC_CLK_TCK
#define _SC_CLK_TCK 2
#endif
#ifndef _SC_COLL_WEIGHTS_MAX
#define _SC_COLL_WEIGHTS_MAX 40
#endif
#ifndef _SC_DELAYTIMER_MAX
#define _SC_DELAYTIMER_MAX 26
#endif
#ifndef _SC_EXPR_NEST_MAX
#define _SC_EXPR_NEST_MAX 42
#endif
#ifndef _SC_FSYNC
#define _SC_FSYNC 15
#endif
#ifndef _SC_GETGR_R_SIZE_MAX
#define _SC_GETGR_R_SIZE_MAX 69
#endif
#ifndef _SC_GETPW_R_SIZE_MAX
#define _SC_GETPW_R_SIZE_MAX 70
#endif
#ifndef _SC_IOV_MAX
#define _SC_IOV_MAX 60
#endif
#ifndef _SC_JOB_CONTROL
#define _SC_JOB_CONTROL 7
#endif
#ifndef _SC_LINE_MAX
#define _SC_LINE_MAX 43
#endif
#ifndef _SC_LOGIN_NAME_MAX
#define _SC_LOGIN_NAME_MAX 71
#endif
#ifndef _SC_MAPPED_FILES
#define _SC_MAPPED_FILES 16
#endif
#ifndef _SC_MEMLOCK
#define _SC_MEMLOCK 17
#endif
#ifndef _SC_MEMLOCK_RANGE
#define _SC_MEMLOCK_RANGE 18
#endif
#ifndef _SC_MEMORY_PROTECTION
#define _SC_MEMORY_PROTECTION 19
#endif
#ifndef _SC_MESSAGE_PASSING
#define _SC_MESSAGE_PASSING 20
#endif
#ifndef _SC_MQ_OPEN_MAX
#define _SC_MQ_OPEN_MAX 27
#endif
#ifndef _SC_MQ_PRIO_MAX
#define _SC_MQ_PRIO_MAX 28
#endif
#ifndef _SC_NGROUPS_MAX
#define _SC_NGROUPS_MAX 3
#endif
#ifndef _SC_NPROCESSORS_CONF
#define _SC_NPROCESSORS_CONF 83
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 84
#endif
#ifndef _SC_OPEN_MAX
#define _SC_OPEN_MAX 4
#endif
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 30
#endif
#ifndef _SC_PAGE_SIZE
#define _SC_PAGE_SIZE 30
#endif
#ifndef _SC_PASS_MAX
#define _SC_PASS_MAX 88
#endif
#ifndef _SC_PRIORITIZED_IO
#define _SC_PRIORITIZED_IO 13
#endif
#ifndef _SC_PRIORITY_SCHEDULING
#define _SC_PRIORITY_SCHEDULING 10
#endif
#ifndef _SC_REALTIME_SIGNALS
#define _SC_REALTIME_SIGNALS 9
#endif
#ifndef _SC_RE_DUP_MAX
#define _SC_RE_DUP_MAX 44
#endif
#ifndef _SC_RTSIG_MAX
#define _SC_RTSIG_MAX 31
#endif
#ifndef _SC_SAVED_IDS
#define _SC_SAVED_IDS 8
#endif
#ifndef _SC_SEMAPHORES
#define _SC_SEMAPHORES 21
#endif
#ifndef _SC_SEM_NSEMS_MAX
#define _SC_SEM_NSEMS_MAX 32
#endif
#ifndef _SC_SEM_VALUE_MAX
#define _SC_SEM_VALUE_MAX 33
#endif
#ifndef _SC_SHARED_MEMORY_OBJECTS
#define _SC_SHARED_MEMORY_OBJECTS 22
#endif
#ifndef _SC_SIGQUEUE_MAX
#define _SC_SIGQUEUE_MAX 34
#endif
#ifndef _SC_STREAM_MAX
#define _SC_STREAM_MAX 5
#endif
#ifndef _SC_SYNCHRONIZED_IO
#define _SC_SYNCHRONIZED_IO 14
#endif
#ifndef _SC_THREADS
#define _SC_THREADS 67
#endif
#ifndef _SC_THREAD_ATTR_STACKADDR
#define _SC_THREAD_ATTR_STACKADDR 77
#endif
#ifndef _SC_THREAD_ATTR_STACKSIZE
#define _SC_THREAD_ATTR_STACKSIZE 78
#endif
#ifndef _SC_THREAD_DESTRUCTOR_ITERATIONS
#define _SC_THREAD_DESTRUCTOR_ITERATIONS 73
#endif
#ifndef _SC_THREAD_KEYS_MAX
#define _SC_THREAD_KEYS_MAX 74
#endif
#ifndef _SC_THREAD_PRIORITY_SCHEDULING
#define _SC_THREAD_PRIORITY_SCHEDULING 79
#endif
#ifndef _SC_THREAD_PRIO_INHERIT
#define _SC_THREAD_PRIO_INHERIT 80
#endif
#ifndef _SC_THREAD_PRIO_PROTECT
#define _SC_THREAD_PRIO_PROTECT 81
#endif
#ifndef _SC_THREAD_SAFE_FUNCTIONS
#define _SC_THREAD_SAFE_FUNCTIONS 68
#endif
#ifndef _SC_THREAD_STACK_MIN
#define _SC_THREAD_STACK_MIN 75
#endif
#ifndef _SC_THREAD_THREADS_MAX
#define _SC_THREAD_THREADS_MAX 76
#endif
#ifndef _SC_TIMERS
#define _SC_TIMERS 11
#endif
#ifndef _SC_TIMER_MAX
#define _SC_TIMER_MAX 35
#endif
#ifndef _SC_TTY_NAME_MAX
#define _SC_TTY_NAME_MAX 72
#endif
#ifndef _SC_TZNAME_MAX
#define _SC_TZNAME_MAX 6
#endif
#ifndef _SC_VERSION
#define _SC_VERSION 29
#endif
#ifndef _SC_XBS5_ILP32_OFF32
#define _SC_XBS5_ILP32_OFF32 125
#endif
#ifndef _SC_XBS5_ILP32_OFFBIG
#define _SC_XBS5_ILP32_OFFBIG 126
#endif
#ifndef _SC_XBS5_LP64_OFF64
#define _SC_XBS5_LP64_OFF64 127
#endif
#ifndef _SC_XBS5_LPBIG_OFFBIG
#define _SC_XBS5_LPBIG_OFFBIG 128
#endif
#ifndef _SC_XOPEN_CRYPT
#define _SC_XOPEN_CRYPT 92
#endif
#ifndef _SC_XOPEN_ENH_I18N
#define _SC_XOPEN_ENH_I18N 93
#endif
#ifndef _SC_XOPEN_LEGACY
#define _SC_XOPEN_LEGACY 129
#endif
#ifndef _SC_XOPEN_REALTIME
#define _SC_XOPEN_REALTIME 130
#endif
#ifndef _SC_XOPEN_REALTIME_THREADS
#define _SC_XOPEN_REALTIME_THREADS 131
#endif
#ifndef _SC_XOPEN_SHM
#define _SC_XOPEN_SHM 94
#endif
#ifndef _SC_XOPEN_UNIX
#define _SC_XOPEN_UNIX 91
#endif
#ifndef _SC_XOPEN_VERSION
#define _SC_XOPEN_VERSION 89
#endif
#ifndef _SC_XOPEN_XCU_VERSION
#define _SC_XOPEN_XCU_VERSION 90
#endif
#endif
