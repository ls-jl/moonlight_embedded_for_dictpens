/*
 * This file is part of Moonlight Embedded.
 *
 * Stubbed mDNS discovery for small Buildroot targets built without Avahi.
 */

#include "discover.h"

void gs_discover_server(char* dest, unsigned short* port) {
  (void)port;
  if (dest)
    dest[0] = 0;

  gs_error = "mDNS discovery is not compiled in; specify the host IP address";
}
