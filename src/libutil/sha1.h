#ifndef _SHA_H
#define _SHA_H

#include <inttypes.h>

/* The SHA block size and message digest sizes, in bytes */

#define SHA_DATASIZE    64
#define SHA_DATALEN     16
#define SHA_DIGESTSIZE  20
#define SHA_DIGESTLEN    5
/* The structure for storing SHA info */

struct sha_ctx {
  uint32_t digest[SHA_DIGESTLEN];  /* Message digest */
  uint32_t count_l, count_h;       /* 64-bit block count */
  uint8_t block[SHA_DATASIZE];     /* SHA data buffer */
  unsigned int index;            /* index into buffer */
};

void sha_init(struct sha_ctx *ctx);
void sha_update(struct sha_ctx *ctx, const unsigned char *buffer, uint32_t len);
void sha_final(struct sha_ctx *ctx);
void sha_digest(struct sha_ctx *ctx, unsigned char *s);
void sha_copy(struct sha_ctx *dest, struct sha_ctx *src);


#endif /* !_SHA_H */
