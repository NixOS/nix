#ifndef _SHA256_H
#define _SHA256_H 1

#include <stdint.h>

#define SHA_LBLOCK	16
#define SHA_CBLOCK	(SHA_LBLOCK*4)	/* SHA treats input data as a
					 * contiguous array of 32 bit
					 * wide big-endian values. */

#define SHA256_CBLOCK	(SHA_LBLOCK*4)	/* SHA-256 treats input data as a
					 * contiguous array of 32 bit
					 * wide big-endian values. */
#define SHA224_DIGEST_LENGTH	28
#define SHA256_DIGEST_LENGTH	32

typedef struct SHA256state_st
	{
	uint32_t h[8];
	uint32_t Nl,Nh;
	uint32_t data[SHA_LBLOCK];
	unsigned int num,md_len;
	} SHA256_CTX;

int SHA224_Init(SHA256_CTX *c);
int SHA224_Update(SHA256_CTX *c, const void *data, size_t len);
int SHA224_Final(unsigned char *md, SHA256_CTX *c);
unsigned char *SHA224(const unsigned char *d, size_t n,unsigned char *md);
int SHA256_Init(SHA256_CTX *c);
int SHA256_Update(SHA256_CTX *c, const void *data, size_t len);
int SHA256_Final(unsigned char *md, SHA256_CTX *c);
unsigned char *SHA256(const unsigned char *d, size_t n,unsigned char *md);
void SHA256_Transform(SHA256_CTX *c, const unsigned char *data);

#endif
