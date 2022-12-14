/*
 * Copyright (C) 2012 Tobias Brunner
 * Copyright (C) 2012 Giuliano Grassi
 * Copyright (C) 2012 Ralf Sager
 * Hochschule fuer Technik Rapperswil
 * Copyright (C) 2012 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <net/if.h>

#include "tun_device.h"

#if !defined(__APPLE__) && !defined(__linux__) && !defined(HAVE_NET_IF_TUN_H)

#warning TUN devices are not supported!

tun_device_t *tun_device_create(const char *name_tmpl)
{
	DBG1(DBG_LIB, "TUN devices are not supported");
	return NULL;
}

#else /* TUN devices supported */

#ifdef __APPLE__
#include <net/if_utun.h>
#include <netinet/in_var.h>
#include <sys/kern_control.h>
#elif defined(__linux__)
#include <linux/if_tun.h>
#else
#include <net/if_tun.h>
#endif

#include <library.h>
#include <utils/debug.h>
#include <threading/thread.h>

#define TUN_DEFAULT_MTU 1500

typedef struct private_tun_device_t private_tun_device_t;

struct private_tun_device_t {

	/**
	 * Public interface
	 */
	tun_device_t public;

	/**
	 * The TUN device's file descriptor
	 */
	int tunfd;

	/**
	 * Name of the TUN device
	 */
	char if_name[IFNAMSIZ];

	/**
	 * Socket used for ioctl() to set interface addr, ...
	 */
	int sock;

	/**
	 * The current MTU
	 */
	int mtu;

	/**
	 * Associated address
	 */
	host_t *address;

	/**
	 * Netmask for address
	 */
	u_int8_t netmask;
};

METHOD(tun_device_t, set_address, bool,
	private_tun_device_t *this, host_t *addr, u_int8_t netmask)
{
	struct ifreq ifr;
	host_t *mask;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	memcpy(&ifr.ifr_addr, addr->get_sockaddr(addr),
		   *addr->get_sockaddr_len(addr));

	if (ioctl(this->sock, SIOCSIFADDR, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to set address on %s: %s",
			 this->if_name, strerror(errno));
		return FALSE;
	}
#ifdef __APPLE__
	if (ioctl(this->sock, SIOCSIFDSTADDR, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to set dest address on %s: %s",
			 this->if_name, strerror(errno));
		return FALSE;
	}
#endif /* __APPLE__ */

	mask = host_create_netmask(addr->get_family(addr), netmask);
	if (!mask)
	{
		DBG1(DBG_LIB, "invalid netmask: %d", netmask);
		return FALSE;
	}
	memcpy(&ifr.ifr_addr, mask->get_sockaddr(mask),
		   *mask->get_sockaddr_len(mask));
	mask->destroy(mask);

	if (ioctl(this->sock, SIOCSIFNETMASK, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to set netmask on %s: %s",
			 this->if_name, strerror(errno));
		return FALSE;
	}
	this->address = addr->clone(addr);
	this->netmask = netmask;
	return TRUE;
}

METHOD(tun_device_t, get_address, host_t*,
	private_tun_device_t *this, u_int8_t *netmask)
{
	if (netmask && this->address)
	{
		*netmask = this->netmask;
	}
	return this->address;
}

METHOD(tun_device_t, up, bool,
	private_tun_device_t *this)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	if (ioctl(this->sock, SIOCGIFFLAGS, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to get interface flags for %s: %s", this->if_name,
			 strerror(errno));
		return FALSE;
	}

	ifr.ifr_flags |= IFF_RUNNING | IFF_UP;

	if (ioctl(this->sock, SIOCSIFFLAGS, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to set interface flags on %s: %s", this->if_name,
			 strerror(errno));
		return FALSE;
	}
	return TRUE;
}

METHOD(tun_device_t, set_mtu, bool,
	private_tun_device_t *this, int mtu)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	ifr.ifr_mtu = mtu;

	if (ioctl(this->sock, SIOCSIFMTU, &ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to set MTU on %s: %s", this->if_name,
			 strerror(errno));
		return FALSE;
	}
	this->mtu = mtu;
	return TRUE;
}

METHOD(tun_device_t, get_mtu, int,
	private_tun_device_t *this)
{
	struct ifreq ifr;

	if (this->mtu > 0)
	{
		return this->mtu;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	this->mtu = TUN_DEFAULT_MTU;

	if (ioctl(this->sock, SIOCGIFMTU, &ifr) == 0)
	{
		this->mtu = ifr.ifr_mtu;
	}
	return this->mtu;
}

METHOD(tun_device_t, get_name, char*,
	private_tun_device_t *this)
{
	return this->if_name;
}

METHOD(tun_device_t, get_fd, int,
	private_tun_device_t *this)
{
	return this->tunfd;
}

METHOD(tun_device_t, write_packet, bool,
	private_tun_device_t *this, chunk_t packet)
{
	ssize_t s;

#ifdef __APPLE__
	/* UTUN's expect the packets to be prepended by a 32-bit protocol number
	 * instead of parsing the packet again, we assume IPv4 for now */
	u_int32_t proto = htonl(AF_INET);
	packet = chunk_cata("cc", chunk_from_thing(proto), packet);
#endif
	s = write(this->tunfd, packet.ptr, packet.len);
	if (s < 0)
	{
		DBG1(DBG_LIB, "failed to write packet to TUN device %s: %s",
			 this->if_name, strerror(errno));
		return FALSE;
	}
	else if (s != packet.len)
	{
		return FALSE;
	}
	return TRUE;
}

METHOD(tun_device_t, read_packet, bool,
	private_tun_device_t *this, chunk_t *packet)
{
	ssize_t len;
	fd_set set;
	bool old;

	FD_ZERO(&set);
	FD_SET(this->tunfd, &set);

	old = thread_cancelability(TRUE);
	len = select(this->tunfd + 1, &set, NULL, NULL, NULL);
	thread_cancelability(old);

	if (len < 0)
	{
		DBG1(DBG_LIB, "select on TUN device %s failed: %s", this->if_name,
			 strerror(errno));
		return FALSE;
	}
	/* FIXME: this is quite expensive for lots of small packets, copy from
	 * local buffer instead? */
	*packet = chunk_alloc(get_mtu(this));
	len = read(this->tunfd, packet->ptr, packet->len);
	if (len < 0)
	{
		DBG1(DBG_LIB, "reading from TUN device %s failed: %s", this->if_name,
			 strerror(errno));
		chunk_free(packet);
		return FALSE;
	}
	packet->len = len;
#ifdef __APPLE__
	/* UTUN's prepend packets with a 32-bit protocol number */
	packet->len -= sizeof(u_int32_t);
	memmove(packet->ptr, packet->ptr + sizeof(u_int32_t), packet->len);
#endif
	return TRUE;
}

METHOD(tun_device_t, destroy, void,
	private_tun_device_t *this)
{
	if (this->tunfd > 0)
	{
		close(this->tunfd);
#ifdef __FreeBSD__
		/* tun(4) says the following: "These network interfaces persist until
		 * the if_tun.ko module is unloaded, or until removed with the
		 * ifconfig(8) command."  So simply closing the FD is not enough. */
		struct ifreq ifr;

		memset(&ifr, 0, sizeof(ifr));
		strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
		ifr.ifr_name[IFNAMSIZ - 1] = '\0';
		if (ioctl(this->sock, SIOCIFDESTROY, &ifr) < 0)
		{
			DBG1(DBG_LIB, "failed to destroy %s: %s", this->if_name,
				 strerror(errno));
		}
#endif /* __FreeBSD__ */
	}
	if (this->sock > 0)
	{
		close(this->sock);
	}
	DESTROY_IF(this->address);
	free(this);
}

/**
 * Initialize the tun device
 */
static bool init_tun(private_tun_device_t *this, const char *name_tmpl)
{
#ifdef __APPLE__

	struct ctl_info info;
	struct sockaddr_ctl addr;
	socklen_t size = IFNAMSIZ;

	memset(&info, 0, sizeof(info));
	memset(&addr, 0, sizeof(addr));

	this->tunfd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
	if (this->tunfd < 0)
	{
		DBG1(DBG_LIB, "failed to open tundevice PF_SYSTEM socket: %s",
			 strerror(errno));
		return FALSE;
	}

	/* get a control identifier for the utun kernel extension */
	strncpy(info.ctl_name, UTUN_CONTROL_NAME, strlen(UTUN_CONTROL_NAME));
	if (ioctl(this->tunfd, CTLIOCGINFO, &info) < 0)
	{
		DBG1(DBG_LIB, "failed to ioctl tundevice: %s", strerror(errno));
		close(this->tunfd);
		return FALSE;
	}

	addr.sc_id = info.ctl_id;
	addr.sc_len = sizeof(addr);
	addr.sc_family = AF_SYSTEM;
	addr.ss_sysaddr = AF_SYS_CONTROL;
	/* allocate identifier dynamically */
	addr.sc_unit = 0;

	if (connect(this->tunfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		DBG1(DBG_LIB, "failed to connect tundevice: %s", strerror(errno));
		close(this->tunfd);
		return FALSE;
	}
	if (getsockopt(this->tunfd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME,
				   this->if_name, &size) < 0)
	{
		DBG1(DBG_LIB, "getting tundevice name failed: %s", strerror(errno));
		close(this->tunfd);
		return FALSE;
	}
	return TRUE;

#elif defined(IFF_TUN)

#if ANDROID
	const char* TUN_DEVICE = "/dev/tun";
#else
	const char* TUN_DEVICE = "/dev/net/tun";
#endif //ANDROID
	struct ifreq ifr;

	strncpy(this->if_name, name_tmpl ?: "tun%d", IFNAMSIZ - 1);
	this->if_name[IFNAMSIZ-1] = '\0';

	this->tunfd = open(TUN_DEVICE, O_RDWR);
	if (this->tunfd < 0)
	{
		DBG1(DBG_LIB, "failed to open %s: %s", TUN_DEVICE, strerror(errno));
		return FALSE;
	}

	memset(&ifr, 0, sizeof(ifr));

	/* TUN device, no packet info */
	ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

	strncpy(ifr.ifr_name, this->if_name, IFNAMSIZ - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';
	if (ioctl(this->tunfd, TUNSETIFF, (void*)&ifr) < 0)
	{
		DBG1(DBG_LIB, "failed to configure TUN device: %s", strerror(errno));
		close(this->tunfd);
		return FALSE;
	}
	strncpy(this->if_name, ifr.ifr_name, IFNAMSIZ - 1);
	this->if_name[IFNAMSIZ - 1] = '\0';

	return TRUE;

#else /* !IFF_TUN */

	/* this works on FreeBSD and might also work on Linux with older TUN
	 * driver versions (no IFF_TUN) */
	char devname[IFNAMSIZ];
	/* the same process is allowed to open a device again, but that's not what
	 * we want (unless we previously closed a device, which we don't know at
	 * this point).  therefore, this counter is static so we don't accidentally
	 * open a device twice */
	static int i = -1;

	if (name_tmpl)
	{
		DBG1(DBG_LIB, "arbitrary naming of TUN devices is not supported");
	}

	for (; ++i < 256; )
	{
		snprintf(devname, IFNAMSIZ, "/dev/tun%d", i);
		this->tunfd = open(devname, O_RDWR);
		if (this->tunfd > 0)
		{	/* for ioctl(2) calls only the interface name is used */
			snprintf(this->if_name, IFNAMSIZ, "tun%d", i);
			break;
		}
		DBG1(DBG_LIB, "failed to open %s: %s", this->if_name, strerror(errno));
	}
	return this->tunfd > 0;

#endif /* !__APPLE__ */
}

/*
 * Described in header
 */
tun_device_t *tun_device_create(const char *name_tmpl)
{
	private_tun_device_t *this;

	INIT(this,
		.public = {
			.read_packet = _read_packet,
			.write_packet = _write_packet,
			.get_mtu = _get_mtu,
			.set_mtu = _set_mtu,
			.get_name = _get_name,
			.get_fd = _get_fd,
			.set_address = _set_address,
			.get_address = _get_address,
			.up = _up,
			.destroy = _destroy,
		},
		.tunfd = -1,
		.sock = -1,
	);

	if (!init_tun(this, name_tmpl))
	{
		free(this);
		return NULL;
	}
	DBG1(DBG_LIB, "created TUN device: %s", this->if_name);

	this->sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (this->sock < 0)
	{
		DBG1(DBG_LIB, "failed to open socket to configure TUN device");
		destroy(this);
		return NULL;
	}
	return &this->public;
}

#endif /* TUN devices supported */
