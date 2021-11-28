#ifndef _SHA_H
#define _SHA_H

#include <inttypes.h>

/* The SHA block size and message digest sizes, in bytes */

#define SHA_DATASIZE    64
#define SHA_DATALEN     16
#define SHA_DIGESTSIZE  20
#define SHA_DIGESTLEN    5
/* The structure for storing SHA info */

struct SHA_CTX {
  uint32_t digest[SHA_DIGESTLEN];  /* Message digest */
  uint32_t count_l, count_h;       /* 64-bit block count */
  uint8_t block[SHA_DATASIZE];     /* SHA data buffer */
  unsigned int index;            /* index into buffer */
};

void SHA1_Init(struct SHA_CTX *ctx);
void SHA1_Update(struct SHA_CTX *ctx, const unsigned char *buffer, uint32_t len);
void SHA1_Final(unsigned char *s, struct SHA_CTX *ctx);
void sha_digest(struct SHA_CTX *ctx, unsigned char *s);
void sha_copy(struct SHA_CTX *dest, struct SHA_CTX *src);


#endif /* !_SHA_H */
