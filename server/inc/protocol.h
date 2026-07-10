#ifndef SERVER_PROTOCOL_H
#define SERVER_PROTOCOL_H

#define SIZE 4096
#define MY_FTP_BOOT "./copy"

typedef enum {
    FTP_CMD_LS    = 1024,
    FTP_CMD_GET   = 1025,
    FTP_CMD_PUT   = 1026,
    FTP_CMD_BYE   = 1027,
    FTP_CMD_LOGIN = 1028,
} cmd_no_t;

unsigned char *read_packet(int fd, int *payload_len);
int send_packet(int fd, int cmd_no, int res_result,
                const unsigned char *data, int data_len);

static inline void put_le32(unsigned char *b, int v) {
    b[0] = (unsigned char)( v        & 0xFF);
    b[1] = (unsigned char)((v >> 8)  & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
}
static inline int get_le32(const unsigned char *b) {
    return b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24);
}

#endif
