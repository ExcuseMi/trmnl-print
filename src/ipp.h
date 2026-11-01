/* An IPP Print-Job request around a PCL stream, for printers that take jobs on port 631 (HTTP POST, Content-Type
   application/ipp). The X's apps have http_request but no raw TCP socket, so port 9100 is out of reach. SPDX-License-Identifier: MIT */
#ifndef IPP_H
#define IPP_H
#include <stddef.h>
#include <stdint.h>

#define IPP_PWG "image/pwg-raster"           /* what the Brother takes (its document-format-supported) */
#define IPP_PCL "application/octet-stream"   /* the fallback: the printer sniffs the language */

/* The request header (version 1.1, Print-Job, request id 1, document-format `mime`); the document bytes
   follow it directly. Returns its length, or 0 if it does not fit in cap or the host/job name is too long. */
size_t ipp_print_job_header(uint8_t *out, size_t cap, const char *host, int port, const char *job_name, const char *mime);
/* The IPP status code of a response (0x0000..0x00ff are success), or -1 if it is not an IPP response */
int ipp_status(const uint8_t *resp, size_t n);
#endif
