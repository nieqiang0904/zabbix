/*
** Zabbix
** Copyright (C) 2001-2015 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "common.h"
#include "comms.h"
#include "log.h"
#include "../zbxcrypto/tls_tcp.h"

#if defined(HAVE_POLARSSL)
#	include <polarssl/error.h>
#endif

#if defined(HAVE_IPV6)
#	define ZBX_SOCKADDR struct sockaddr_storage
#else
#	define ZBX_SOCKADDR struct sockaddr_in
#endif

#if !defined(ZBX_SOCKLEN_T)
#	define ZBX_SOCKLEN_T socklen_t
#endif

#if !defined(SOCK_CLOEXEC)
#	define SOCK_CLOEXEC 0	/* SOCK_CLOEXEC is Linux-specific, available since 2.6.23 */
#endif

#if defined(HAVE_OPENSSL)
extern ZBX_THREAD_LOCAL char	info_buf[256];
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_strerror                                              *
 *                                                                            *
 * Purpose: return string describing tcp error                                *
 *                                                                            *
 * Return value: pointer to the null terminated string                        *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/

#define ZBX_SOCKET_STRERROR_LEN	256

static char	zbx_socket_strerror_message[ZBX_SOCKET_STRERROR_LEN];

const char	*zbx_socket_strerror(void)
{
	zbx_socket_strerror_message[ZBX_SOCKET_STRERROR_LEN - 1] = '\0';	/* force null termination */
	return zbx_socket_strerror_message;
}

#ifdef HAVE___VA_ARGS__
#	define zbx_set_socket_strerror(fmt, ...) __zbx_zbx_set_socket_strerror(ZBX_CONST_STRING(fmt), ##__VA_ARGS__)
#else
#	define zbx_set_socket_strerror __zbx_zbx_set_socket_strerror
#endif
static void	__zbx_zbx_set_socket_strerror(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	zbx_vsnprintf(zbx_socket_strerror_message, sizeof(zbx_socket_strerror_message), fmt, args);

	va_end(args);
}

#if !defined(_WINDOWS)
/******************************************************************************
 *                                                                            *
 * Function: zbx_gethost_by_ip                                                *
 *                                                                            *
 * Purpose: retrieve 'hostent' by IP address                                  *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_IPV6)
void	zbx_gethost_by_ip(const char *ip, char *host, size_t hostlen)
{
	struct addrinfo	hints, *ai = NULL;

	assert(ip);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;

	if (0 != getaddrinfo(ip, NULL, &hints, &ai))
	{
		host[0] = '\0';
		goto out;
	}

	if (0 != getnameinfo(ai->ai_addr, ai->ai_addrlen, host, hostlen, NULL, 0, NI_NAMEREQD))
	{
		host[0] = '\0';
		goto out;
	}
out:
	if (NULL != ai)
		freeaddrinfo(ai);
}
#else
void	zbx_gethost_by_ip(const char *ip, char *host, size_t hostlen)
{
	struct in_addr	addr;
	struct hostent  *hst;

	assert(ip);

	if (0 == inet_aton(ip, &addr))
	{
		host[0] = '\0';
		return;
	}

	if (NULL == (hst = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET)))
	{
		host[0] = '\0';
		return;
	}

	zbx_strlcpy(host, hst->h_name, hostlen);
}
#endif	/* HAVE_IPV6 */
#endif	/* _WINDOWS */

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_start                                                 *
 *                                                                            *
 * Purpose: Initialize Windows Sockets APIs                                   *
 *                                                                            *
 * Return value: SUCCEED or FAIL - an error occurred                          *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
#if defined(_WINDOWS)

#define ZBX_SOCKET_START()	if (FAIL == socket_started) socket_started = zbx_socket_start()

int	socket_started = FAIL;	/* winXX threads require socket_started not to be static */

static int	zbx_socket_start()
{
	WSADATA	sockInfo;
	int	ret;

	if (0 != (ret = WSAStartup(MAKEWORD(2, 2), &sockInfo)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "WSAStartup() failed: %s", strerror_from_system(ret));
		return FAIL;
	}

	return SUCCEED;
}

#else
#	define ZBX_SOCKET_START()
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_clean                                                 *
 *                                                                            *
 * Purpose: initialize socket                                                 *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_clean(zbx_socket_t *s)
{
	memset(s, 0, sizeof(zbx_socket_t));

	s->buf_type = ZBX_BUF_TYPE_STAT;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	s->tls_ctx = NULL;
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_free                                                  *
 *                                                                            *
 * Purpose: free socket's dynamic buffer                                      *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_free(zbx_socket_t *s)
{
	if (ZBX_BUF_TYPE_DYN == s->buf_type)
		zbx_free(s->buffer);
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_init                                                     *
 *                                                                            *
 * Purpose: initialize structure of zabbix socket with specified socket       *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	zbx_tcp_init(zbx_socket_t *s, ZBX_SOCKET o)
{
	zbx_socket_clean(s);

	s->socket = o;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_timeout_set                                           *
 *                                                                            *
 * Purpose: set timeout for socket operations                                 *
 *                                                                            *
 * Parameters: s       - [IN] socket descriptor                               *
 *             timeout - [IN] timeout, in seconds                             *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_timeout_set(zbx_socket_t *s, int timeout)
{
	s->timeout = timeout;
#if defined(_WINDOWS)
	timeout *= 1000;

	if (ZBX_PROTO_ERROR == setsockopt(s->socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "setsockopt() failed for SO_RCVTIMEO: %s",
				strerror_from_system(zbx_socket_last_error()));
	}

	if (ZBX_PROTO_ERROR == setsockopt(s->socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout, sizeof(timeout)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "setsockopt() failed for SO_SNDTIMEO: %s",
				strerror_from_system(zbx_socket_last_error()));
	}
#else
	alarm(timeout);
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_timeout_cleanup                                       *
 *                                                                            *
 * Purpose: clean up timeout for socket operations                            *
 *                                                                            *
 * Parameters: s - [OUT] socket descriptor                                    *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
static void	zbx_socket_timeout_cleanup(zbx_socket_t *s)
{
#if !defined(_WINDOWS)
	if (0 != s->timeout)
	{
		alarm(0);
		s->timeout = 0;
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_connect                                               *
 *                                                                            *
 * Purpose: connect to the specified address with an optional timeout value   *
 *                                                                            *
 * Parameters: s       - [IN] socket descriptor                               *
 *             addr    - [IN] the address                                     *
 *             addrlen - [IN] the length of addr structure                    *
 *             timeout - [IN] the connection timeout (0 - system default)     *
 *             error   - [OUT] the error message                              *
 *                                                                            *
 * Return value: SUCCEED - connected successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Comments: Windows connect implementation uses internal timeouts which      *
 *           cannot be changed. Because of that in Windows use nonblocking    *
 *           connect, then wait for connection the specified timeout period   *
 *           and if successful change socket back to blocking mode.           *
 *                                                                            *
 ******************************************************************************/
static int	zbx_socket_connect(zbx_socket_t *s, const struct sockaddr *addr, socklen_t addrlen, int timeout,
		char **error)
{
#if defined(_WINDOWS)
	u_long		mode = 1;
	FD_SET		fdw, fde;
	int		res;
	struct timeval	tv, *ptv;
#endif
	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

#if defined(_WINDOWS)
	if (0 != ioctlsocket(s->socket, FIONBIO, &mode))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	FD_ZERO(&fdw);
	FD_SET(s->socket, &fdw);

	FD_ZERO(&fde);
	FD_SET(s->socket, &fde);

	if (0 != timeout)
	{
		tv.tv_sec = timeout;
		tv.tv_usec = 0;
		ptv = &tv;
	}
	else
		ptv = NULL;

	if (ZBX_PROTO_ERROR == connect(s->socket, addr, addrlen) && WSAEWOULDBLOCK != zbx_socket_last_error())
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	if (-1 == (res = select(0, NULL, &fdw, &fde, ptv)))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	if (0 == FD_ISSET(s->socket, &fdw))
	{
		if (0 != FD_ISSET(s->socket, &fde))
			*error = zbx_strdup(*error, "Connection refused.");
		else
			*error = zbx_strdup(*error, "A connection timeout occurred.");

		return FAIL;
	}

	mode = 0;
	if (0 != ioctlsocket(s->socket, FIONBIO, &mode))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}
#else
	if (ZBX_PROTO_ERROR == connect(s->socket, addr, addrlen))
	{
		*error = zbx_strdup(*error, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}
#endif

	s->connection_type = ZBX_TCP_SEC_UNENCRYPTED;

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_create                                                *
 *                                                                            *
 * Purpose: connect the socket of the specified type to external host         *
 *                                                                            *
 * Parameters: s - [OUT] socket descriptor                                    *
 *                                                                            *
 * Return value: SUCCEED - connected successfully                             *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_IPV6)
static int	zbx_socket_create(zbx_socket_t *s, int type, const char *source_ip, const char *ip, unsigned short port,
		int timeout, unsigned int tls_connect, char *tls_arg1, char *tls_arg2)
{
	int		ret = FAIL;
	struct addrinfo	*ai = NULL, hints;
	struct addrinfo	*ai_bind = NULL;
	char		service[8], *error = NULL;
	void		(*func_socket_close)(zbx_socket_t *s);

	ZBX_SOCKET_START();

	zbx_socket_clean(s);

	zbx_snprintf(service, sizeof(service), "%d", port);
	memset(&hints, 0x00, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = type;

	if (0 != getaddrinfo(ip, service, &hints, &ai))
	{
		zbx_set_socket_strerror("cannot resolve [%s]", ip);
		goto out;
	}

	if (ZBX_SOCKET_ERROR == (s->socket = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol)))
	{
		zbx_set_socket_strerror("cannot create socket [[%s]:%d]: %s",
				ip, port, strerror_from_system(zbx_socket_last_error()));
		goto out;
	}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
	fcntl(s->socket, F_SETFD, FD_CLOEXEC);
#endif
	func_socket_close = (SOCK_STREAM == type ? zbx_tcp_close : zbx_udp_close);

	if (NULL != source_ip)
	{
		memset(&hints, 0x00, sizeof(struct addrinfo));

		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = type;
		hints.ai_flags = AI_NUMERICHOST;

		if (0 != getaddrinfo(source_ip, NULL, &hints, &ai_bind))
		{
			zbx_set_socket_strerror("invalid source IP address [%s]", source_ip);
			func_socket_close(s);
			goto out;
		}

		if (ZBX_PROTO_ERROR == bind(s->socket, ai_bind->ai_addr, ai_bind->ai_addrlen))
		{
			zbx_set_socket_strerror("bind() failed: %s", strerror_from_system(zbx_socket_last_error()));
			func_socket_close(s);
			goto out;
		}
	}

	if (SUCCEED != zbx_socket_connect(s, ai->ai_addr, ai->ai_addrlen, timeout, &error))
	{
		func_socket_close(s);
		zbx_set_socket_strerror("cannot connect to [[%s]:%d]: %s", ip, port, error);
		zbx_free(error);
		goto out;
	}

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (SUCCEED != zbx_tls_connect(s, &error, tls_connect, tls_arg1, tls_arg2))
		{
			zbx_tcp_close(s);
			zbx_set_socket_strerror("%s", error);
			zbx_free(error);
			goto out;
		}
#else
		zbx_tcp_close(s);
		zbx_set_socket_strerror("Support for TLS was not compiled in.");
		goto out;
#endif
	}

	ret = SUCCEED;
out:
	if (NULL != ai)
		freeaddrinfo(ai);

	if (NULL != ai_bind)
		freeaddrinfo(ai_bind);

	return ret;
}
#else
static int	zbx_socket_create(zbx_socket_t *s, int type, const char *source_ip, const char *ip, unsigned short port,
		int timeout, unsigned int tls_connect, char *tls_arg1, char *tls_arg2)
{
	ZBX_SOCKADDR	servaddr_in;
	struct hostent	*hp;
	char		*error = NULL;
	void		(*func_socket_close)(zbx_socket_t *s);

	ZBX_SOCKET_START();

	zbx_socket_clean(s);

	if (NULL == (hp = gethostbyname(ip)))
	{
#if defined(_WINDOWS)
		zbx_set_socket_strerror("gethostbyname() failed for '%s': %s",
				ip, strerror_from_system(WSAGetLastError()));
#elif defined(HAVE_HSTRERROR)
		zbx_set_socket_strerror("gethostbyname() failed for '%s': [%d] %s",
				ip, h_errno, hstrerror(h_errno));
#else
		zbx_set_socket_strerror("gethostbyname() failed for '%s': [%d]",
				ip, h_errno);
#endif
		return FAIL;
	}

	servaddr_in.sin_family = AF_INET;
	servaddr_in.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	servaddr_in.sin_port = htons(port);

	if (ZBX_SOCKET_ERROR == (s->socket = socket(AF_INET, type | SOCK_CLOEXEC, 0)))
	{
		zbx_set_socket_strerror("cannot create socket [[%s]:%d]: %s",
				ip, port, strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
	fcntl(s->socket, F_SETFD, FD_CLOEXEC);
#endif
	func_socket_close = (SOCK_STREAM == type ? zbx_tcp_close : zbx_udp_close);

	if (NULL != source_ip)
	{
		ZBX_SOCKADDR	source_addr;

		memset(&source_addr, 0, sizeof(source_addr));

		source_addr.sin_family = AF_INET;
		source_addr.sin_addr.s_addr = inet_addr(source_ip);
		source_addr.sin_port = 0;

		if (ZBX_PROTO_ERROR == bind(s->socket, (struct sockaddr *)&source_addr, sizeof(source_addr)))
		{
			zbx_set_socket_strerror("bind() failed: %s", strerror_from_system(zbx_socket_last_error()));
			func_socket_close(s);
			return FAIL;
		}
	}

	if (SUCCEED != zbx_socket_connect(s, (struct sockaddr *)&servaddr_in, sizeof(servaddr_in), timeout, &error))
	{
		func_socket_close(s);
		zbx_set_socket_strerror("cannot connect to [[%s]:%d]: %s", ip, port, error);
		zbx_free(error);
		return FAIL;
	}

	if (ZBX_TCP_SEC_TLS_CERT == tls_connect || ZBX_TCP_SEC_TLS_PSK == tls_connect)
	{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (SUCCEED != zbx_tls_connect(s, &error, tls_connect, tls_arg1, tls_arg2))
		{
			zbx_tcp_close(s);
			zbx_set_socket_strerror("%s", error);
			zbx_free(error);
			return FAIL;
		}
#else
		zbx_tcp_close(s);
		zbx_set_socket_strerror("Support for TLS was not compiled in.");
		return FAIL;
#endif
	}

	return SUCCEED;
}
#endif	/* HAVE_IPV6 */

int	zbx_tcp_connect(zbx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout,
		unsigned int tls_connect, char *tls_arg1, char *tls_arg2)
{
	return zbx_socket_create(s, SOCK_STREAM, source_ip, ip, port, timeout, tls_connect, tls_arg1, tls_arg2);
}

static ssize_t	zbx_tls_write(zbx_socket_t *s, const char *buf, size_t len)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL == s->tls_ctx)		/* unencrypted connection */
	{
#endif
		ssize_t	res;

		if (ZBX_PROTO_ERROR == (res = ZBX_TCP_WRITE(s->socket, buf, len)))
		{
			zbx_set_socket_strerror("ZBX_TCP_WRITE() failed: %s",
					strerror_from_system((unsigned long)zbx_socket_last_error()));
		}

		return res;
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	}
	else	/* TLS connection */
	{
#if defined(HAVE_POLARSSL)
		int	res;

		do
		{
			res = ssl_write(s->tls_ctx, (const unsigned char *)buf, len);
		}
		while (POLARSSL_ERR_NET_WANT_WRITE == res);

		if (0 > res)
		{
			char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

			polarssl_strerror(res, err, sizeof(err));
			zbx_set_socket_strerror("ssl_write() failed: %s", err);

			return ZBX_PROTO_ERROR;
		}
		else
			return (ssize_t)res;
#elif defined(HAVE_GNUTLS)
		ssize_t	res;

		do
		{
			res = gnutls_record_send(s->tls_ctx, buf, len);
		}
		while (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res);

		if (0 > res)
		{
#if defined(_WINDOWS)
			zbx_set_socket_strerror("gnutls_record_send() failed: %Id %s", res, gnutls_strerror(res));
#else
			zbx_set_socket_strerror("gnutls_record_send() failed: %zd %s", res, gnutls_strerror(res));
#endif
			return ZBX_PROTO_ERROR;
		}
		else
			return (ssize_t)res;
#elif defined(HAVE_OPENSSL)
		int	res;

		if (0 < (res = SSL_write(s->tls_ctx, buf, (int)len)))
		{
			return (ssize_t)res;	/* success */
		}
		else
		{
			/* SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE should not be returned here because we set */
			/* SSL_MODE_AUTO_RETRY flag in zbx_tls_init_child() */

			int	error_code;

			error_code = SSL_get_error(s->tls_ctx, res);

			if (0 == res && SSL_ERROR_ZERO_RETURN == error_code)
			{
				zbx_set_socket_strerror("connection closed during write");

				return ZBX_PROTO_ERROR;
			}
			else
			{
				char	*error = NULL;
				size_t	error_alloc = 0, error_offset = 0;

				info_buf[0] = '\0';	/* empty buffer for zbx_openssl_info_cb() messages */
				zbx_snprintf_alloc(&error, &error_alloc, &error_offset, "TLS write returned error code "
						"%d:", error_code);
				zbx_tls_error_msg(&error, &error_alloc, &error_offset);
				zbx_set_socket_strerror("%s: %s", error, info_buf);
				zbx_free(error);

				return ZBX_PROTO_ERROR;
			}
		}
#endif
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_send_ext                                                 *
 *                                                                            *
 * Purpose: send data                                                         *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments:                                                                  *
 *     RFC 5246 "The Transport Layer Security (TLS) Protocol. Version 1.2"    *
 *     says: "The record layer fragments information blocks into TLSPlaintext *
 *     records carrying data in chunks of 2^14 bytes or less.".               *
 *                                                                            *
 *     This function combines sending of Zabbix protocol header (5 bytes),    *
 *     data length (8 bytes) and at least part of the message into one block  *
 *     of up to 16384 bytes for efficiency. The same is applied for sending   *
 *     unencrypted messages.                                                  *
 *                                                                            *
 ******************************************************************************/

#define ZBX_TCP_HEADER_DATA	"ZBXD"
#define ZBX_TCP_HEADER_VERSION	"\1"
#define ZBX_TCP_HEADER		ZBX_TCP_HEADER_DATA ZBX_TCP_HEADER_VERSION
#define ZBX_TCP_HEADER_LEN	5

int	zbx_tcp_send_ext(zbx_socket_t *s, const char *data, size_t len, unsigned char flags, int timeout)
{
#define ZBX_TCP_MAX_FIRST_MSG_LEN	16384

	zbx_uint64_t	len64_le;
	ssize_t		i = 0, written = 0;
	int		ret = SUCCEED;

	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	if (0 != (flags & ZBX_TCP_PROTOCOL))
	{
		char	header_buf[ZBX_TCP_MAX_FIRST_MSG_LEN];	/* Buffer is allocated on stack with a hope that it   */
								/* will be short-lived in CPU cache. Static buffer is */
								/* not used on purpose.				      */
		size_t	take_bytes;

		memcpy(header_buf, ZBX_TCP_HEADER, ZBX_TCP_HEADER_LEN);

		len64_le = zbx_htole_uint64((zbx_uint64_t)len);
		memcpy(header_buf + ZBX_TCP_HEADER_LEN, &len64_le, sizeof(len64_le));

		take_bytes = MIN(len, ZBX_TCP_MAX_FIRST_MSG_LEN - ZBX_TCP_HEADER_LEN - sizeof(len64_le));
		memcpy(header_buf + ZBX_TCP_HEADER_LEN + sizeof(len64_le), data, take_bytes);

		if (ZBX_PROTO_ERROR == (i = zbx_tls_write(s, header_buf, ZBX_TCP_HEADER_LEN + sizeof(len64_le) +
				take_bytes)))
		{
			ret = FAIL;
			goto cleanup;
		}

		written += i - ZBX_TCP_HEADER_LEN - (ssize_t)sizeof(len64_le);
	}

	while (written < (ssize_t)len)
	{
		if (ZBX_PROTO_ERROR == (i = zbx_tls_write(s, data + written, len - (size_t)written)))
		{
			ret = FAIL;
			goto cleanup;
		}
		written += i;
	}
cleanup:
	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return ret;

#undef ZBX_TCP_MAX_FIRST_MSG_LEN
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_close                                                    *
 *                                                                            *
 * Purpose: close open TCP socket                                             *
 *                                                                            *
 * Author: Alexei Vladishev                                                   *
 *                                                                            *
 ******************************************************************************/
void	zbx_tcp_close(zbx_socket_t *s)
{
	zbx_tcp_unaccept(s);

	zbx_socket_timeout_cleanup(s);

	zbx_socket_free(s);
	zbx_socket_close(s->socket);
}

/******************************************************************************
 *                                                                            *
 * Function: get_address_family                                               *
 *                                                                            *
 * Purpose: return address family                                             *
 *                                                                            *
 * Parameters: addr - [IN] address or hostname                                *
 *             family - [OUT] address family                                  *
 *             error - [OUT] error string                                     *
 *             max_error_len - [IN] error string length                       *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexander Vladishev                                                *
 *                                                                            *
 ******************************************************************************/
#ifdef HAVE_IPV6
int	get_address_family(const char *addr, int *family, char *error, int max_error_len)
{
	struct addrinfo	hints, *ai = NULL;
	int		err, res = FAIL;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = 0;
	hints.ai_socktype = SOCK_STREAM;

	if (0 != (err = getaddrinfo(addr, NULL, &hints, &ai)))
	{
		zbx_snprintf(error, max_error_len, "%s: [%d] %s", addr, err, gai_strerror(err));
		goto out;
	}

	if (PF_INET != ai->ai_family && PF_INET6 != ai->ai_family)
	{
		zbx_snprintf(error, max_error_len, "%s: unsupported address family", addr);
		goto out;
	}

	*family = (int)ai->ai_family;

	res = SUCCEED;
out:
	if (NULL != ai)
		freeaddrinfo(ai);

	return res;
}
#endif	/* HAVE_IPV6 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_listen                                                   *
 *                                                                            *
 * Purpose: create socket for listening                                       *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Alexei Vladishev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
#if defined(HAVE_IPV6)
int	zbx_tcp_listen(zbx_socket_t *s, const char *listen_ip, unsigned short listen_port)
{
	struct addrinfo	hints, *ai = NULL, *current_ai;
	char		port[8], *ip, *ips, *delim;
	int		i, err, on, ret = FAIL;

	ZBX_SOCKET_START();

	zbx_socket_clean(s);

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = PF_UNSPEC;
	hints.ai_flags = AI_NUMERICHOST | AI_PASSIVE;
	hints.ai_socktype = SOCK_STREAM;
	zbx_snprintf(port, sizeof(port), "%hu", listen_port);

	ip = ips = (NULL == listen_ip ? NULL : strdup(listen_ip));

	while (1)
	{
		delim = (NULL == ip ? NULL : strchr(ip, ','));
		if (NULL != delim)
			*delim = '\0';

		if (0 != (err = getaddrinfo(ip, port, &hints, &ai)))
		{
			zbx_set_socket_strerror("cannot resolve address [[%s]:%s]: [%d] %s",
					ip ? ip : "-", port, err, gai_strerror(err));
			goto out;
		}

		for (current_ai = ai; NULL != current_ai; current_ai = current_ai->ai_next)
		{
			if (ZBX_SOCKET_COUNT == s->num_socks)
			{
				zbx_set_socket_strerror("not enough space for socket [[%s]:%s]",
						ip ? ip : "-", port);
				goto out;
			}

			if (PF_INET != current_ai->ai_family && PF_INET6 != current_ai->ai_family)
				continue;

			if (ZBX_SOCKET_ERROR == (s->sockets[s->num_socks] =
					socket(current_ai->ai_family, current_ai->ai_socktype | SOCK_CLOEXEC,
					current_ai->ai_protocol)))
			{
				zbx_set_socket_strerror("socket() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
#ifdef _WINDOWS
				if (WSAEAFNOSUPPORT == zbx_socket_last_error())
#else
				if (EAFNOSUPPORT == zbx_socket_last_error())
#endif
					continue;
				else
					goto out;
			}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
			fcntl(s->sockets[s->num_socks], F_SETFD, FD_CLOEXEC);
#endif
			/* enable address reuse */
			/* this is to immediately use the address even if it is in TIME_WAIT state */
			/* http://www-128.ibm.com/developerworks/linux/library/l-sockpit/index.html */
			on = 1;
			if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_REUSEADDR,
					(void *)&on, sizeof(on)))
			{
				zbx_set_socket_strerror("setsockopt() with SO_REUSEADDR for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
			}

#if defined(IPPROTO_IPV6) && defined(IPV6_V6ONLY)
			if (PF_INET6 == current_ai->ai_family &&
					ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], IPPROTO_IPV6,
					IPV6_V6ONLY, (void *)&on, sizeof(on)))
			{
				zbx_set_socket_strerror("setsockopt() with IPV6_V6ONLY for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
			}
#endif
			if (ZBX_PROTO_ERROR == bind(s->sockets[s->num_socks], current_ai->ai_addr,
					current_ai->ai_addrlen))
			{
				zbx_set_socket_strerror("bind() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				zbx_socket_close(s->sockets[s->num_socks]);
#ifdef _WINDOWS
				if (WSAEADDRINUSE == zbx_socket_last_error())
#else
				if (EADDRINUSE == zbx_socket_last_error())
#endif
					continue;
				else
					goto out;
			}

			if (ZBX_PROTO_ERROR == listen(s->sockets[s->num_socks], SOMAXCONN))
			{
				zbx_set_socket_strerror("listen() for [[%s]:%s] failed: %s",
						ip ? ip : "-", port, strerror_from_system(zbx_socket_last_error()));
				zbx_socket_close(s->sockets[s->num_socks]);
				goto out;
			}

			s->num_socks++;
		}

		if (NULL != ai)
		{
			freeaddrinfo(ai);
			ai = NULL;
		}

		if (NULL == ip || NULL == delim)
			break;

		*delim = ',';
		ip = delim + 1;
	}

	if (0 == s->num_socks)
	{
		zbx_set_socket_strerror("zbx_tcp_listen() fatal error: unable to serve on any address [[%s]:%hu]",
				listen_ip ? listen_ip : "-", listen_port);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != ips)
		zbx_free(ips);

	if (NULL != ai)
		freeaddrinfo(ai);

	if (SUCCEED != ret)
	{
		for (i = 0; i < s->num_socks; i++)
			zbx_socket_close(s->sockets[i]);
	}

	return ret;
}
#else
int	zbx_tcp_listen(zbx_socket_t *s, const char *listen_ip, unsigned short listen_port)
{
	ZBX_SOCKADDR	serv_addr;
	char		*ip, *ips, *delim;
	int		i, on, ret = FAIL;

	ZBX_SOCKET_START();

	zbx_socket_clean(s);

	ip = ips = (NULL == listen_ip ? NULL : strdup(listen_ip));

	while (1)
	{
		delim = (NULL == ip ? NULL : strchr(ip, ','));
		if (NULL != delim)
			*delim = '\0';

		if (NULL != ip && FAIL == is_ip4(ip))
		{
			zbx_set_socket_strerror("incorrect IPv4 address [%s]", ip);
			goto out;
		}

		if (ZBX_SOCKET_COUNT == s->num_socks)
		{
			zbx_set_socket_strerror("not enough space for socket [[%s]:%hu]",
					ip ? ip : "-", listen_port);
			goto out;
		}

		if (ZBX_SOCKET_ERROR == (s->sockets[s->num_socks] = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)))
		{
			zbx_set_socket_strerror("socket() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
			goto out;
		}

#if !defined(_WINDOWS) && !SOCK_CLOEXEC
		fcntl(s->sockets[s->num_socks], F_SETFD, FD_CLOEXEC);
#endif
		/* enable address reuse */
		/* this is to immediately use the address even if it is in TIME_WAIT state */
		/* http://www-128.ibm.com/developerworks/linux/library/l-sockpit/index.html */
		on = 1;
		if (ZBX_PROTO_ERROR == setsockopt(s->sockets[s->num_socks], SOL_SOCKET, SO_REUSEADDR, (void *)&on,
				sizeof(on)))
		{
			zbx_set_socket_strerror("setsockopt() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
		}

		memset(&serv_addr, 0, sizeof(serv_addr));

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_addr.s_addr = (NULL != ip ? inet_addr(ip) : htonl(INADDR_ANY));
		serv_addr.sin_port = htons((unsigned short)listen_port);

		if (ZBX_PROTO_ERROR == bind(s->sockets[s->num_socks], (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
		{
			zbx_set_socket_strerror("bind() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
			zbx_socket_close(s->sockets[s->num_socks]);
			goto out;
		}

		if (ZBX_PROTO_ERROR == listen(s->sockets[s->num_socks], SOMAXCONN))
		{
			zbx_set_socket_strerror("listen() for [[%s]:%hu] failed: %s",
					ip ? ip : "-", listen_port, strerror_from_system(zbx_socket_last_error()));
			zbx_socket_close(s->sockets[s->num_socks]);
			goto out;
		}

		s->num_socks++;

		if (NULL == ip || NULL == delim)
			break;
		*delim = ',';
		ip = delim + 1;
	}

	if (0 == s->num_socks)
	{
		zbx_set_socket_strerror("zbx_tcp_listen() fatal error: unable to serve on any address [[%s]:%hu]",
				listen_ip ? listen_ip : "-", listen_port);
		goto out;
	}

	ret = SUCCEED;
out:
	if (NULL != ips)
		zbx_free(ips);

	if (SUCCEED != ret)
	{
		for (i = 0; i < s->num_socks; i++)
			zbx_socket_close(s->sockets[i]);
	}

	return ret;
}
#endif	/* HAVE_IPV6 */

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_accept                                                   *
 *                                                                            *
 * Purpose: permits an incoming connection attempt on a socket                *
 *                                                                            *
 * Return value: SUCCEED - success                                            *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev, Aleksandrs Saveljevs                             *
 *                                                                            *
 ******************************************************************************/
int	zbx_tcp_accept(zbx_socket_t *s, unsigned int tls_accept)
{
	ZBX_SOCKADDR	serv_addr;
	fd_set		sock_set;
	ZBX_SOCKET	accepted_socket;
	ZBX_SOCKLEN_T	nlen;
	int		i, n = 0;
	unsigned char	buf;		/* 1 byte buffer */

	zbx_tcp_unaccept(s);

	FD_ZERO(&sock_set);

	for (i = 0; i < s->num_socks; i++)
	{
		FD_SET(s->sockets[i], &sock_set);
#if !defined(_WINDOWS)
		if (s->sockets[i] > n)
			n = s->sockets[i];
#endif
	}

	if (ZBX_PROTO_ERROR == select(n + 1, &sock_set, NULL, NULL, NULL))
	{
		zbx_set_socket_strerror("select() failed: %s", strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	for (i = 0; i < s->num_socks; i++)
	{
		if (FD_ISSET(s->sockets[i], &sock_set))
			break;
	}

	/* Since this socket was returned by select(), we know we have */
	/* a connection waiting and that this accept() will not block. */
	nlen = sizeof(serv_addr);
	if (ZBX_SOCKET_ERROR == (accepted_socket = (ZBX_SOCKET)accept(s->sockets[i], (struct sockaddr *)&serv_addr,
			&nlen)))
	{
		zbx_set_socket_strerror("accept() failed: %s", strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}

	s->socket_orig = s->socket;	/* remember main socket */
	s->socket = accepted_socket;	/* replace socket to accepted */
	s->accepted = 1;

	/* if the 1st byte is 0x16 then assume it's a TLS connection */
	if (1 == recv(s->socket, &buf, 1, MSG_PEEK) && '\x16' == buf)
	{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
		if (0 != (tls_accept & ZBX_TCP_SEC_TLS_CERT) || 0 != (tls_accept & ZBX_TCP_SEC_TLS_PSK))
		{
			char	*error = NULL;

			if (SUCCEED != zbx_tls_accept(s, &error, tls_accept))
			{
				zbx_set_socket_strerror("from %s: %s", get_ip_by_socket(s), error);
				zbx_tcp_unaccept(s);
				zbx_free(error);
				return FAIL;
			}
		}
		else
		{
			zbx_set_socket_strerror("from %s: TLS connections are not allowed by configuration.",
					get_ip_by_socket(s));
			zbx_tcp_unaccept(s);
			return FAIL;
		}
#else
		zbx_set_socket_strerror("from %s: support for TLS was not compiled in.", get_ip_by_socket(s));
		zbx_tcp_unaccept(s);
		return FAIL;
#endif
	}
	else
	{
		if (0 == (tls_accept & ZBX_TCP_SEC_UNENCRYPTED))
		{
			zbx_set_socket_strerror("from %s: unencrypted connections are not allowed.",
					get_ip_by_socket(s));
			zbx_tcp_unaccept(s);
			return FAIL;
		}

		s->connection_type = ZBX_TCP_SEC_UNENCRYPTED;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_unaccept                                                 *
 *                                                                            *
 * Purpose: close accepted connection                                         *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
void	zbx_tcp_unaccept(zbx_socket_t *s)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL != s->tls_ctx)
		zbx_tls_close(s);
#endif
	if (!s->accepted) return;

	shutdown(s->socket, 2);

	zbx_socket_close(s->socket);

	s->socket = s->socket_orig;	/* restore main socket */
	s->socket_orig = ZBX_SOCKET_ERROR;
	s->accepted = 0;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_socket_find_line                                             *
 *                                                                            *
 * Purpose: finds the next line in socket data buffer                         *
 *                                                                            *
 * Parameters: s - [IN] the socket                                            *
 *                                                                            *
 * Return value: A pointer to the next line or NULL if the socket data buffer *
 *               contains no more lines.                                      *
 *                                                                            *
 ******************************************************************************/
static const char	*zbx_socket_find_line(zbx_socket_t *s)
{
	char	*ptr, *line = NULL;

	if (NULL == s->next_line)
		return NULL;

	/* check if the buffer contains the next line */
	if ((size_t)(s->next_line - s->buffer) <= s->read_bytes && NULL != (ptr = strchr(s->next_line, '\n')))
	{
		line = s->next_line;
		s->next_line = ptr + 1;

		if (ptr > line && '\r' == *(ptr - 1))
			ptr--;

		*ptr = '\0';
	}

	return line;
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_recv_line                                                *
 *                                                                            *
 * Purpose: reads next line from a socket                                     *
 *                                                                            *
 * Parameters: s - [IN] the socket                                            *
 *                                                                            *
 * Return value: a pointer to the line in socket buffer or NULL if there are  *
 *               no more lines (socket was closed or an error occurred)       *
 *                                                                            *
 * Comments: Lines larger than 64KB are truncated.                            *
 *                                                                            *
 ******************************************************************************/
const char	*zbx_tcp_recv_line(zbx_socket_t *s)
{
#define ZBX_TCP_LINE_LEN	(64 * ZBX_KIBIBYTE)

	char		buffer[ZBX_STAT_BUF_LEN], *ptr = NULL;
	const char	*line;
	ssize_t		nbytes;
	size_t		alloc = 0, offset = 0, line_length, left;

	/* check if the buffer already contains the next line */
	if (NULL != (line = zbx_socket_find_line(s)))
		return line;

	/* Find the size of leftover data from the last read line operation and copy */
	/* the leftover data to the static buffer and reset the dynamic buffer.      */
	/* Because we are reading data in ZBX_STAT_BUF_LEN chunks the leftover       */
	/* data will always fit the static buffer.                                   */
	if (NULL != s->next_line)
	{
		left = s->read_bytes - (s->next_line - s->buffer);
		memmove(s->buf_stat, s->next_line, left);
	}
	else
		left = 0;

	s->read_bytes = left;
	s->next_line = s->buf_stat;

	zbx_socket_free(s);
	s->buf_type = ZBX_BUF_TYPE_STAT;
	s->buffer = s->buf_stat;

	/* read more data into static buffer */
	if (ZBX_PROTO_ERROR == (nbytes = ZBX_TCP_READ(s->socket, s->buf_stat + left, ZBX_STAT_BUF_LEN - left - 1)))
		goto out;

	s->buf_stat[left + nbytes] = '\0';

	if (0 == nbytes)
	{
		/* Socket was closed before newline was found. If we have data in buffer  */
		/* return it with success. Otherwise return failure.                      */
		line = 0 != s->read_bytes ? s->next_line : NULL;
		s->next_line += s->read_bytes;

		goto out;
	}

	s->read_bytes += nbytes;

	/* check if the static buffer now contains the next line */
	if (NULL != (line = zbx_socket_find_line(s)))
		goto out;

	/* copy the static buffer data into dynamic buffer */
	s->buf_type = ZBX_BUF_TYPE_DYN;
	s->buffer = NULL;
	zbx_strncpy_alloc(&s->buffer, &alloc, &offset, s->buf_stat, s->read_bytes);
	line_length = s->read_bytes;

	/* Read data into dynamic buffer until newline has been found. */
	/* Lines larger than ZBX_TCP_LINE_LEN bytes will be truncated. */
	do
	{
		if (ZBX_PROTO_ERROR == (nbytes = ZBX_TCP_READ(s->socket, buffer, ZBX_STAT_BUF_LEN - 1)))
			goto out;

		if (0 == nbytes)
		{
			/* socket was closed before newline was found, just return the data we have */
			line = 0 != s->read_bytes ? s->buffer : NULL;
			s->next_line = s->buffer + s->read_bytes;

			goto out;
		}

		buffer[nbytes] = '\0';
		ptr = strchr(buffer, '\n');

		if (s->read_bytes + nbytes < ZBX_TCP_LINE_LEN && s->read_bytes == line_length)
		{
			zbx_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, nbytes);
			s->read_bytes += nbytes;
		}
		else
		{
			if (0 != (left = MIN(ZBX_TCP_LINE_LEN - s->read_bytes, ptr - buffer)))
			{
				/* fill the string to the defined limit */
				zbx_strncpy_alloc(&s->buffer, &alloc, &offset, buffer, left);
				s->read_bytes += left;
			}

			/* if the line exceeds the defined limit then truncate it by skipping data until the newline */
			if (NULL != ptr)
			{
				zbx_strncpy_alloc(&s->buffer, &alloc, &offset, ptr, nbytes - (ptr - buffer));
				s->read_bytes += nbytes - (ptr - buffer);
			}
		}

		line_length += nbytes;

	}
	while (NULL == ptr);

	s->next_line = s->buffer;
	line = zbx_socket_find_line(s);
out:
	return line;
}

static ssize_t	zbx_tls_read(zbx_socket_t *s, char *buf, size_t len)
{
#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	if (NULL == s->tls_ctx)		/* unencrypted connection */
	{
#endif
		ssize_t	res;

		if (ZBX_PROTO_ERROR == (res = ZBX_TCP_READ(s->socket, buf, len)))
		{
			zbx_set_socket_strerror("ZBX_TCP_READ() failed: %s",
					strerror_from_system((unsigned long)zbx_socket_last_error()));
		}

		return res;

#if defined(HAVE_POLARSSL) || defined(HAVE_GNUTLS) || defined(HAVE_OPENSSL)
	}
	else	/* TLS connection */
	{
#if defined(HAVE_POLARSSL)
		int	res;

		do
		{
			res = ssl_read(s->tls_ctx, (unsigned char *)buf, len);
		}
		while (POLARSSL_ERR_NET_WANT_READ == res);

		if (0 > res)
		{
			char	err[128];	/* 128 bytes are enough for PolarSSL error messages */

			polarssl_strerror(res, err, sizeof(err));
			zbx_set_socket_strerror("ssl_read() failed: %s", err);

			return ZBX_PROTO_ERROR;
		}
		else
			return (ssize_t)res;
#elif defined(HAVE_GNUTLS)
		ssize_t	res;

		do
		{
			res = gnutls_record_recv(s->tls_ctx, buf, len);
		}
		while (GNUTLS_E_INTERRUPTED == res || GNUTLS_E_AGAIN == res);	/* TODO maybe GNUTLS_E_REHANDSHAKE */

		if (0 > res)
		{
#if defined(_WINDOWS)
			zbx_set_socket_strerror("gnutls_record_recv() failed: %Id %s", res, gnutls_strerror(res));
#else
			zbx_set_socket_strerror("gnutls_record_recv() failed: %zd %s", res, gnutls_strerror(res));
#endif
			return ZBX_PROTO_ERROR;
		}
		else
			return (ssize_t)res;
#elif defined(HAVE_OPENSSL)
		int	res;

		if (0 < (res = SSL_read(s->tls_ctx, buf, len)))
		{
			return (ssize_t)res;	/* success */
		}
		else
		{
			/* SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE should not be returned here because we set */
			/* SSL_MODE_AUTO_RETRY flag in zbx_tls_init_child() */

			int	error_code;

			error_code = SSL_get_error(s->tls_ctx, res);

			if (0 == res && SSL_ERROR_ZERO_RETURN == error_code)
			{
				zbx_set_socket_strerror("connection closed during read");

				return ZBX_PROTO_ERROR;
			}
			else
			{
				char	*error = NULL;
				size_t	error_alloc = 0, error_offset = 0;

				info_buf[0] = '\0';	/* empty buffer for zbx_openssl_info_cb() messages */
				zbx_snprintf_alloc(&error, &error_alloc, &error_offset, "TLS read returned error code "
						"%d:", error_code);
				zbx_tls_error_msg(&error, &error_alloc, &error_offset);
				zbx_set_socket_strerror("%s: %s", error, info_buf);
				zbx_free(error);

				return ZBX_PROTO_ERROR;
			}
		}
#endif
	}
#endif
}

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_recv_ext                                                 *
 *                                                                            *
 * Purpose: receive data                                                      *
 *                                                                            *
 * Return value: number of bytes received - success,                          *
 *               FAIL - an error occurred                                     *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 ******************************************************************************/
ssize_t	zbx_tcp_recv_ext(zbx_socket_t *s, unsigned char flags, int timeout)
{
#define ZBX_BUF_LEN	(ZBX_STAT_BUF_LEN * 8)
	ssize_t		nbytes, left, total_bytes;
	size_t		allocated, offset, read_bytes;
	zbx_uint64_t	expected_len;

	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	zbx_socket_free(s);

	total_bytes = 0;
	read_bytes = 0;

	s->buf_type = ZBX_BUF_TYPE_STAT;
	s->buffer = s->buf_stat;

	left = ZBX_TCP_HEADER_LEN;

	if (ZBX_PROTO_ERROR == (nbytes = zbx_tls_read(s, s->buf_stat, left)))
		goto out;

	if (ZBX_TCP_HEADER_LEN == nbytes && 0 == strncmp(s->buf_stat, ZBX_TCP_HEADER, ZBX_TCP_HEADER_LEN))
	{
		total_bytes += nbytes;

		left = sizeof(zbx_uint64_t);
		if (left != (nbytes = zbx_tls_read(s, (char *)&expected_len, left)))
		{
			total_bytes = FAIL;
			goto out;
		}

		expected_len = zbx_letoh_uint64(expected_len);

		if (ZBX_MAX_RECV_DATA_SIZE < expected_len)
		{
			zabbix_log(LOG_LEVEL_WARNING, "Message size " ZBX_FS_UI64 " from %s"
					" exceeds the maximum size " ZBX_FS_UI64 " bytes. Message ignored.",
					expected_len, get_ip_by_socket(s), (zbx_uint64_t)ZBX_MAX_RECV_DATA_SIZE);
			total_bytes = FAIL;
			goto cleanup;
		}

		flags |= ZBX_TCP_READ_UNTIL_CLOSE;
	}
	else
	{
		read_bytes = nbytes;
		expected_len = 16 * ZBX_MEBIBYTE;
	}

	s->buf_stat[read_bytes] = '\0';

	if (0 != (flags & ZBX_TCP_READ_UNTIL_CLOSE))
	{
		if (0 == nbytes)
			goto cleanup;
	}
	else
	{
		if (nbytes < left)
			goto cleanup;
	}

	left = sizeof(s->buf_stat) - read_bytes - 1;

	/* check for an empty socket if exactly ZBX_TCP_HEADER_LEN bytes (without a header) were sent */
	if (0 == read_bytes || '\n' != s->buf_stat[read_bytes - 1])	/* requests to passive agents end with '\n' */
	{
		/* fill static buffer */
		while (read_bytes < expected_len && 0 < left &&
				ZBX_PROTO_ERROR != (nbytes = zbx_tls_read(s, s->buf_stat + read_bytes, left)))
		{
			read_bytes += nbytes;

			if (0 != (flags & ZBX_TCP_READ_UNTIL_CLOSE))
			{
				if (0 == nbytes)
					break;
			}
			else
			{
				if (nbytes < left)	/* should we stop reading? */
				{
					/* XML protocol? */
					if (0 == strncmp(s->buf_stat, "<req>", ZBX_CONST_STRLEN("<req>")))
					{
						/* closing tag received in the last 10 bytes? */
						s->buf_stat[read_bytes] = '\0';
						if (NULL != strstr(s->buf_stat + read_bytes -
								(10 > read_bytes ? read_bytes : 10), "</req>"))
							break;
					}
					else
						break;
				}
			}

			left -= nbytes;
		}
	}

	s->buf_stat[read_bytes] = '\0';

	if (sizeof(s->buf_stat) - 1 == read_bytes)	/* static buffer is full */
	{
		allocated = ZBX_BUF_LEN;
		s->buf_type = ZBX_BUF_TYPE_DYN;
		s->buffer = zbx_malloc(NULL, allocated);

		memcpy(s->buffer, s->buf_stat, sizeof(s->buf_stat));

		offset = read_bytes;

		/* fill dynamic buffer */
		while (read_bytes < expected_len &&
				ZBX_PROTO_ERROR != (nbytes = zbx_tls_read(s, s->buf_stat, sizeof(s->buf_stat))))
		{
			zbx_strncpy_alloc(&s->buffer, &allocated, &offset, s->buf_stat, nbytes);
			read_bytes += nbytes;

			if (0 != (flags & ZBX_TCP_READ_UNTIL_CLOSE))
			{
				if (0 == nbytes)
					break;
			}
			else
			{
				if ((size_t)nbytes < sizeof(s->buf_stat) - 1)	/* should we stop reading? */
				{
					/* XML protocol? */
					if (0 == strncmp(s->buffer, "<req>", ZBX_CONST_STRLEN("<req>")))
					{
						/* closing tag received in the last 10 bytes? */
						if (NULL != strstr(s->buffer + read_bytes - 10, "</req>"))
							break;
					}
					else
						break;
				}
			}
		}
	}
out:
	if (ZBX_PROTO_ERROR == nbytes)
		total_bytes = FAIL;
cleanup:
	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	if (FAIL != total_bytes)
	{
		total_bytes += read_bytes;
		s->read_bytes = read_bytes;
	}

	return total_bytes;
}

char	*get_ip_by_socket(zbx_socket_t *s)
{
	ZBX_SOCKADDR			sa;
	ZBX_SOCKLEN_T			sz = sizeof(sa);
	ZBX_THREAD_LOCAL static char	host[64];
	char				*error_message = NULL;

	if (ZBX_PROTO_ERROR == getpeername(s->socket, (struct sockaddr*)&sa, &sz))
	{
		error_message = strerror_from_system(zbx_socket_last_error());
		zbx_set_socket_strerror("connection rejected, getpeername() failed: %s", error_message);
		goto out;
	}

#if defined(HAVE_IPV6)
	if (0 != zbx_getnameinfo((struct sockaddr *)&sa, host, sizeof(host), NULL, 0, NI_NUMERICHOST))
	{
		error_message = strerror_from_system(zbx_socket_last_error());
		zbx_set_socket_strerror("connection rejected, getnameinfo() failed: %s", error_message);
	}
#else
	zbx_snprintf(host, sizeof(host), "%s", inet_ntoa(sa.sin_addr));
#endif

out:
	if (NULL != error_message)
	{
		zabbix_log(LOG_LEVEL_WARNING, "Cannot get socket IP address: %s", error_message);
		strscpy(host, "unknown IP");
	}

	return host;
}

#if defined(HAVE_IPV6)
static int	zbx_ip_cmp(const struct addrinfo *current_ai, ZBX_SOCKADDR name)
{
	/* Network Byte Order is ensured */
	/* IPv4-compatible, the first 96 bits are zeros */
	const unsigned char	ipv4_compat_mask[12] = {0};
	/* IPv4-mapped, the first 80 bits are zeros, 16 next - ones */
	const unsigned char	ipv4_mapped_mask[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

	struct sockaddr_in	*name4 = (struct sockaddr_in *)&name,
				*ai_addr4 = (struct sockaddr_in *)current_ai->ai_addr;
	struct sockaddr_in6	*name6 = (struct sockaddr_in6 *)&name,
				*ai_addr6 = (struct sockaddr_in6 *)current_ai->ai_addr;

#ifdef HAVE_SOCKADDR_STORAGE_SS_FAMILY
	if (current_ai->ai_family == name.ss_family)
#else
	if (current_ai->ai_family == name.__ss_family)
#endif
	{
		switch (current_ai->ai_family)
		{
			case AF_INET:
				if (name4->sin_addr.s_addr == ai_addr4->sin_addr.s_addr)
				{
					return SUCCEED;
				}
				break;
			case AF_INET6:
				if (0 == memcmp(name6->sin6_addr.s6_addr, ai_addr6->sin6_addr.s6_addr,
						sizeof(struct in6_addr)))
				{
					return SUCCEED;
				}
				break;
		}
	}
	else
	{
		switch (current_ai->ai_family)
		{
			case AF_INET:
				/* incoming AF_INET6, must see whether it is compatible or mapped */
				if ((0 == memcmp(name6->sin6_addr.s6_addr, ipv4_compat_mask, 12) ||
						0 == memcmp(name6->sin6_addr.s6_addr, ipv4_mapped_mask, 12)) &&
						0 == memcmp(&name6->sin6_addr.s6_addr[12],
						(unsigned char *)&ai_addr4->sin_addr.s_addr, 4))
				{
					return SUCCEED;
				}
				break;
			case AF_INET6:
				/* incoming AF_INET, must see whether the given is compatible or mapped */
				if ((0 == memcmp(ai_addr6->sin6_addr.s6_addr, ipv4_compat_mask, 12) ||
						0 == memcmp(ai_addr6->sin6_addr.s6_addr, ipv4_mapped_mask, 12)) &&
						0 == memcmp(&ai_addr6->sin6_addr.s6_addr[12],
						(unsigned char *)&name4->sin_addr.s_addr, 4))
				{
					return SUCCEED;
				}
				break;
		}
	}

	return FAIL;
}
#endif

/******************************************************************************
 *                                                                            *
 * Function: zbx_tcp_check_security                                           *
 *                                                                            *
 * Purpose: check if connection initiator is in list of IP addresses          *
 *                                                                            *
 * Parameters: s - socket descriptor                                          *
 *             ip_list - comma-delimited list of IP addresses                 *
 *             allow_if_empty - allow connection if no IP given               *
 *                                                                            *
 * Return value: SUCCEED - connection allowed                                 *
 *               FAIL - connection is not allowed                             *
 *                                                                            *
 * Author: Alexei Vladishev, Dmitry Borovikov                                 *
 *                                                                            *
 * Comments: standard, compatible and IPv4-mapped addresses are treated       *
 *           the same: 127.0.0.1 == ::127.0.0.1 == ::ffff:127.0.0.1           *
 *                                                                            *
 ******************************************************************************/
int	zbx_tcp_check_security(zbx_socket_t *s, const char *ip_list, int allow_if_empty)
{
#if defined(HAVE_IPV6)
	struct addrinfo	hints, *ai = NULL, *current_ai;
#else
	struct hostent	*hp;
	int		i;
#endif
	ZBX_SOCKADDR	name;
	ZBX_SOCKLEN_T	nlen;

	char		tmp[MAX_STRING_LEN], *start = NULL, *end = NULL;

	if (1 == allow_if_empty && (NULL == ip_list || '\0' == *ip_list))
		return SUCCEED;

	nlen = sizeof(name);

	if (ZBX_PROTO_ERROR == getpeername(s->socket, (struct sockaddr *)&name, &nlen))
	{
		zbx_set_socket_strerror("connection rejected, getpeername() failed: %s",
				strerror_from_system(zbx_socket_last_error()));
		return FAIL;
	}
	else
	{
		strscpy(tmp, ip_list);

		for (start = tmp; '\0' != *start;)
		{
			if (NULL != (end = strchr(start, ',')))
				*end = '\0';

			/* allow IP addresses or DNS names for authorization */
#if defined(HAVE_IPV6)
			memset(&hints, 0, sizeof(hints));
			hints.ai_family = PF_UNSPEC;
			if (0 == getaddrinfo(start, NULL, &hints, &ai))
			{
				for (current_ai = ai; NULL != current_ai; current_ai = current_ai->ai_next)
				{
					if (SUCCEED == zbx_ip_cmp(current_ai, name))
					{
						freeaddrinfo(ai);
						return SUCCEED;
					}
				}
				freeaddrinfo(ai);
			}
#else
			if (NULL != (hp = gethostbyname(start)))
			{
				for (i = 0; NULL != hp->h_addr_list[i]; i++)
				{
					if (name.sin_addr.s_addr == ((struct in_addr *)hp->h_addr_list[i])->s_addr)
						return SUCCEED;
				}
			}
#endif	/* HAVE_IPV6 */
			if (NULL != end)
			{
				*end = ',';
				start = end + 1;
			}
			else
				break;
		}

		if (NULL != end)
			*end = ',';
	}
#if defined(HAVE_IPV6)
	if (0 == zbx_getnameinfo((struct sockaddr *)&name, tmp, sizeof(tmp), NULL, 0, NI_NUMERICHOST))
		zbx_set_socket_strerror("connection from \"%s\" rejected, allowed hosts: \"%s\"", tmp, ip_list);
	else
		zbx_set_socket_strerror("connection rejected, allowed hosts: \"%s\"", ip_list);
#else
	zbx_set_socket_strerror("connection from \"%s\" rejected, allowed hosts: \"%s\"",
			inet_ntoa(name.sin_addr), ip_list);
#endif
	return FAIL;
}

int	zbx_udp_connect(zbx_socket_t *s, const char *source_ip, const char *ip, unsigned short port, int timeout)
{
	return zbx_socket_create(s, SOCK_DGRAM, source_ip, ip, port, timeout, ZBX_TCP_SEC_UNENCRYPTED, NULL, NULL);
}

int	zbx_udp_send(zbx_socket_t *s, const char *data, size_t data_len, int timeout)
{
	int	ret = SUCCEED;

	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	if (ZBX_PROTO_ERROR == sendto(s->socket, data, data_len, 0, NULL, 0))
	{
		zbx_set_socket_strerror("sendto() failed: %s", strerror_from_system(zbx_socket_last_error()));
		ret = FAIL;
	}

	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	return ret;
}

int	zbx_udp_recv(zbx_socket_t *s, int timeout)
{
	char	buffer[65508];	/* maximum payload for UDP over IPv4 is 65507 bytes */
	ssize_t	read_bytes;

	zbx_socket_free(s);

	if (0 != timeout)
		zbx_socket_timeout_set(s, timeout);

	if (ZBX_PROTO_ERROR == (read_bytes = recvfrom(s->socket, buffer, sizeof(buffer) - 1, 0, NULL, NULL)))
		zbx_set_socket_strerror("recvfrom() failed: %s", strerror_from_system(zbx_socket_last_error()));

	if (0 != timeout)
		zbx_socket_timeout_cleanup(s);

	if (ZBX_PROTO_ERROR == read_bytes)
		return FAIL;

	if (sizeof(s->buf_stat) > read_bytes)
	{
		s->buf_type = ZBX_BUF_TYPE_STAT;
		s->buffer = s->buf_stat;
	}
	else
	{
		s->buf_type = ZBX_BUF_TYPE_DYN;
		s->buffer = zbx_malloc(s->buffer, read_bytes + 1);
	}

	buffer[read_bytes] = '\0';
	memcpy(s->buffer, buffer, read_bytes + 1);

	s->read_bytes = (size_t)read_bytes;

	return SUCCEED;
}

void	zbx_udp_close(zbx_socket_t *s)
{
	zbx_socket_timeout_cleanup(s);

	zbx_socket_free(s);
	zbx_socket_close(s->socket);
}
