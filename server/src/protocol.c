#define _GNU_SOURCE
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/*  read_packet – mirrors the client exactly                         */
/* ------------------------------------------------------------------ */
unsigned char *read_packet(int fd, int *payload_len)
{
    unsigned char ch;
    /* find 0xC0 header */
    while (1) {
        if (read(fd, &ch, 1) <= 0) return NULL;
        if (ch == 0xC0) break;
    }
    /* consume duplicate 0xC0 (trailer of previous packet) */
    while (1) {
        if (read(fd, &ch, 1) <= 0) return NULL;
        if (ch != 0xC0) break;
    }
    /* ch is the first byte of pkg_len (little-endian) */
    int pkg_len = ch;
    for (int i = 1; i < 4; i++) {
        if (read(fd, &ch, 1) <= 0) return NULL;
        pkg_len |= (ch << (8 * i));
    }
    if (pkg_len < 10) return NULL;  /* minimum valid packet */

    int plen = pkg_len - 6;          /* exclude header(1)+len(4)+trailer(1) */
    if (plen <= 0) return NULL;

    unsigned char *payload = (unsigned char *)malloc((size_t)plen);
    if (!payload) return NULL;

    int off = 0;
    while (off < plen) {
        if (read(fd, &ch, 1) <= 0) { free(payload); return NULL; }
        if (ch == 0xC0) break;       /* trailer */
        payload[off++] = ch;
    }
    if (off != plen) { free(payload); return NULL; }

    *payload_len = plen;
    return payload;
}

/* ------------------------------------------------------------------ */
/*  send_packet – build + send a response                             */
/* ------------------------------------------------------------------ */
int send_packet(int fd, int cmd_no, int res_result,
                const unsigned char *data, int data_len)
{
    /*
     * Wire format:
     *   0xC0 + pkg_len(4) + cmd_no(4) + res_len(4) + res_result(1) + data + 0xC0
     */
    int res_len = 1 + data_len;       /* res_result byte + data */
    int pkg_len = 14 + data_len;      /* header(1)+len(4)+cmd(4)+res_len(4)+res_result(1)+data+trailer(1) */

    unsigned char *pkt = (unsigned char *)malloc((size_t)pkg_len);
    if (!pkt) return -1;

    int i = 0;
    pkt[i++] = 0xC0;
    put_le32(pkt + i, pkg_len); i += 4;
    put_le32(pkt + i, cmd_no);  i += 4;
    put_le32(pkt + i, res_len); i += 4;
    pkt[i++] = (unsigned char)res_result;
    if (data && data_len > 0) {
        memcpy(pkt + i, data, (size_t)data_len);
        i += data_len;
    }
    pkt[i++] = 0xC0;

    int ret = (int)write(fd, pkt, (size_t)pkg_len);
    free(pkt);
    return (ret == pkg_len) ? 0 : -1;
}
