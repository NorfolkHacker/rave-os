#ifndef ACID_HAL_NETWORK_H
#define ACID_HAL_NETWORK_H

/* This device's real hostname and primary non-loopback IPv4 address, for a
 * Network app to show. Writes a best-effort hostname into hostname_out
 * (hostname_len bytes) and either a dotted-quad address or "none" into
 * ip_out (ip_len bytes) either way -- both buffers are always
 * NUL-terminated and safe to display, regardless of the return value.
 * Returns 1 if a real address was found (this device is actually on a
 * network), 0 otherwise. Deliberately just an address, not a live
 * connectivity/socket check -- acid OS v2 has no network stack of its own
 * yet (no sockets, no HTTP client, nothing an app could actually use a
 * connection for); this reports what the underlying OS/hardware already
 * knows, honestly, rather than pretending to check reachability this
 * project can't yet act on. */
int hal_network_get_info( char * hostname_out, int hostname_len,
                           char * ip_out, int ip_len );

#endif
