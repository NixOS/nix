/* $Id$ */

/* sha.c - Implementation of the Secure Hash Algorithm
 *
 * Copyright (C) 1995, A.M. Kuchling
 *
 * Distribute and use freely; there are no restrictions on further 
 * dissemination and usage except those imposed by the laws of your 
 * country of residence.
 *
 * Adapted to pike and some cleanup by Niels Möller.
 */

/* $Id$ */

/* SHA: NIST's Secure Hash Algorithm */

/* Based on SHA code originally posted to sci.crypt by Peter Gutmann
   in message <30ajo5$oe8@ccu2.auckland.ac.nz>.
   Modified to test for endianness on creation of SHA objects by AMK.
   Also, the original specification of SHA was found to have a weakness
   by NSA/NIST.  This code implements the fixed version of SHA.
*/

/* Here's the first paragraph of Peter Gutmann's posting:
   
The following is my SHA (FIPS 180) code updated to allow use of the "fixed"
SHA, thanks to Jim Gillogly and an anonymous contributor for the information on
what's changed in the new version.  The fix is a simple change which involves
adding a single rotate in the initial expansion function.  It is unknown
whether this is an optimal solution to the problem which was discovered in the
SHA or whether it's simply a bandaid which fixes the problem with a minimum of
effort (for example the reengineering of a great many Capstone chips).
*/

#include "sha1.h"

#include <string.h>

void sha_copy(struct SHA_CTX *dest, struct SHA_CTX *src)
{
  unsigned int i;

  dest->count_l=src->count_l;
  dest->count_h=src->count_h;
  for(i=0; i<SHA_DIGESTLEN; i++)
    dest->digest[i]=src->digest[i];
  for(i=0; i < src->index; i++)
    dest->block[i] = src->block[i];
  dest->index = src->index;
}


/* The SHA f()-functions.  The f1 and f3 functions can be optimized to
   save one boolean operation each - thanks to Rich Schroeppel,
   rcs@cs.arizona.edu for discovering this */

/*#define f1(x,y,z) ( ( x & y ) | ( ~x & z ) )          // Rounds  0-19 */
#define f1(x,y,z)   ( z ^ ( x & ( y ^ z ) ) )           /* Rounds  0-19 */
#define f2(x,y,z)   ( x ^ y ^ z )                       /* Rounds 20-39 */
/*#define f3(x,y,z) ( ( x & y ) | ( x & z ) | ( y & z ) )   // Rounds 40-59 */
#define f3(x,y,z)   ( ( x & y ) | ( z & ( x | y ) ) )   /* Rounds 40-59 */
#define f4(x,y,z)   ( x ^ y ^ z )                       /* Rounds 60-79 */

/* The SHA Mysterious Constants */

#define K1  0x5A827999L                                 /* Rounds  0-19 */
#define K2  0x6ED9EBA1L                                 /* Rounds 20-39 */
#define K3  0x8F1BBCDCL                                 /* Rounds 40-59 */
#define K4  0xCA62C1D6L                                 /* Rounds 60-79 */

/* SHA initial values */

#define h0init  0x67452301L
#define h1init  0xEFCDAB89L
#define h2init  0x98BADCFEL
#define h3init  0x10325476L
#define h4init  0xC3D2E1F0L

/* 32-bit rotate left - kludged with shifts */

#define ROTL(n,X)  ( ( (X) << (n) ) | ( (X) >> ( 32 - (n) ) ) )

/* The initial expanding function.  The hash function is defined over an
   80-word expanded input array W, where the first 16 are copies of the input
   data, and the remaining 64 are defined by

        W[ i ] = W[ i - 16 ] ^ W[ i - 14 ] ^ W[ i - 8 ] ^ W[ i - 3 ]

   This implementation generates these values on the fly in a circular
   buffer - thanks to Colin Plumb, colin@nyx10.cs.du.edu for this
   optimization.

   The updated SHA changes the expanding function by adding a rotate of 1
   bit.  Thanks to Jim Gillogly, jim@rand.org, and an anonymous contributor
   for this information */

#define expand(W,i) ( W[ i & 15 ] = \
		      ROTL( 1, ( W[ i & 15 ] ^ W[ (i - 14) & 15 ] ^ \
				 W[ (i - 8) & 15 ] ^ W[ (i - 3) & 15 ] ) ) )


/* The prototype SHA sub-round.  The fundamental sub-round is:

        a' = e + ROTL( 5, a ) + f( b, c, d ) + k + data;
        b' = a;
        c' = ROTL( 30, b );
        d' = c;
        e' = d;

   but this is implemented by unrolling the loop 5 times and renaming the
   variables ( e, a, b, c, d ) = ( a', b', c', d', e' ) each iteration.
   This code is then replicated 20 times for each of the 4 functions, using
   the next 20 values from the W[] array each time */

#define subRound(a, b, c, d, e, f, k, data) \
    ( e += ROTL( 5, a ) + f( b, c, d ) + k + data, b = ROTL( 30, b ) )

/* Initialize the SHA values */

void SHA1_Init(struct SHA_CTX *ctx)
{
  /* Set the h-vars to their initial values */
  ctx->digest[ 0 ] = h0init;
  ctx->digest[ 1 ] = h1init;
  ctx->digest[ 2 ] = h2init;
  ctx->digest[ 3 ] = h3init;
  ctx->digest[ 4 ] = h4init;

  /* Initialize bit count */
  ctx->count_l = ctx->count_h = 0;
  
  /* Initialize buffer */
  ctx->index = 0;
}

/* Perform the SHA transformation.  Note that this code, like MD5, seems to
   break some optimizing compilers due to the complexity of the expressions
   and the size of the basic block.  It may be necessary to split it into
   sections, e.g. based on the four subrounds

   Note that this function destroys the data area */

static void sha_transform(struct SHA_CTX *ctx, uint32_t *data )
{
  uint32_t A, B, C, D, E;     /* Local vars */

  /* Set up first buffer and local data buffer */
  A = ctx->digest[0];
  B = ctx->digest[1];
  C = ctx->digest[2];
  D = ctx->digest[3];
  E = ctx->digest[4];

  /* Heavy mangling, in 4 sub-rounds of 20 interations each. */
  subRound( A, B, C, D, E, f1, K1, data[ 0] );
  subRound( E, A, B, C, D, f1, K1, data[ 1] );
  subRound( D, E, A, B, C, f1, K1, data[ 2] );
  subRound( C, D, E, A, B, f1, K1, data[ 3] );
  subRound( B, C, D, E, A, f1, K1, data[ 4] );
  subRound( A, B, C, D, E, f1, K1, data[ 5] );
  subRound( E, A, B, C, D, f1, K1, data[ 6] );
  subRound( D, E, A, B, C, f1, K1, data[ 7] );
  subRound( C, D, E, A, B, f1, K1, data[ 8] );
  subRound( B, C, D, E, A, f1, K1, data[ 9] );
  subRound( A, B, C, D, E, f1, K1, data[10] );
  subRound( E, A, B, C, D, f1, K1, data[11] );
  subRound( D, E, A, B, C, f1, K1, data[12] );
  subRound( C, D, E, A, B, f1, K1, data[13] );
  subRound( B, C, D, E, A, f1, K1, data[14] );
  subRound( A, B, C, D, E, f1, K1, data[15] );
  subRound( E, A, B, C, D, f1, K1, expand( data, 16 ) );
  subRound( D, E, A, B, C, f1, K1, expand( data, 17 ) );
  subRound( C, D, E, A, B, f1, K1, expand( data, 18 ) );
  subRound( B, C, D, E, A, f1, K1, expand( data, 19 ) );

  subRound( A, B, C, D, E, f2, K2, expand( data, 20 ) );
  subRound( E, A, B, C, D, f2, K2, expand( data, 21 ) );
  subRound( D, E, A, B, C, f2, K2, expand( data, 22 ) );
  subRound( C, D, E, A, B, f2, K2, expand( data, 23 ) );
  subRound( B, C, D, E, A, f2, K2, expand( data, 24 ) );
  subRound( A, B, C, D, E, f2, K2, expand( data, 25 ) );
  subRound( E, A, B, C, D, f2, K2, expand( data, 26 ) );
  subRound( D, E, A, B, C, f2, K2, expand( data, 27 ) );
  subRound( C, D, E, A, B, f2, K2, expand( data, 28 ) );
  subRound( B, C, D, E, A, f2, K2, expand( data, 29 ) );
  subRound( A, B, C, D, E, f2, K2, expand( data, 30 ) );
  subRound( E, A, B, C, D, f2, K2, expand( data, 31 ) );
  subRound( D, E, A, B, C, f2, K2, expand( data, 32 ) );
  subRound( C, D, E, A, B, f2, K2, expand( data, 33 ) );
  subRound( B, C, D, E, A, f2, K2, expand( data, 34 ) );
  subRound( A, B, C, D, E, f2, K2, expand( data, 35 ) );
  subRound( E, A, B, C, D, f2, K2, expand( data, 36 ) );
  subRound( D, E, A, B, C, f2, K2, expand( data, 37 ) );
  subRound( C, D, E, A, B, f2, K2, expand( data, 38 ) );
  subRound( B, C, D, E, A, f2, K2, expand( data, 39 ) );

  subRound( A, B, C, D, E, f3, K3, expand( data, 40 ) );
  subRound( E, A, B, C, D, f3, K3, expand( data, 41 ) );
  subRound( D, E, A, B, C, f3, K3, expand( data, 42 ) );
  subRound( C, D, E, A, B, f3, K3, expand( data, 43 ) );
  subRound( B, C, D, E, A, f3, K3, expand( data, 44 ) );
  subRound( A, B, C, D, E, f3, K3, expand( data, 45 ) );
  subRound( E, A, B, C, D, f3, K3, expand( data, 46 ) );
  subRound( D, E, A, B, C, f3, K3, expand( data, 47 ) );
  subRound( C, D, E, A, B, f3, K3, expand( data, 48 ) );
  subRound( B, C, D, E, A, f3, K3, expand( data, 49 ) );
  subRound( A, B, C, D, E, f3, K3, expand( data, 50 ) );
  subRound( E, A, B, C, D, f3, K3, expand( data, 51 ) );
  subRound( D, E, A, B, C, f3, K3, expand( data, 52 ) );
  subRound( C, D, E, A, B, f3, K3, expand( data, 53 ) );
  subRound( B, C, D, E, A, f3, K3, expand( data, 54 ) );
  subRound( A, B, C, D, E, f3, K3, expand( data, 55 ) );
  subRound( E, A, B, C, D, f3, K3, expand( data, 56 ) );
  subRound( D, E, A, B, C, f3, K3, expand( data, 57 ) );
  subRound( C, D, E, A, B, f3, K3, expand( data, 58 ) );
  subRound( B, C, D, E, A, f3, K3, expand( data, 59 ) );

  subRound( A, B, C, D, E, f4, K4, expand( data, 60 ) );
  subRound( E, A, B, C, D, f4, K4, expand( data, 61 ) );
  subRound( D, E, A, B, C, f4, K4, expand( data, 62 ) );
  subRound( C, D, E, A, B, f4, K4, expand( data, 63 ) );
  subRound( B, C, D, E, A, f4, K4, expand( data, 64 ) );
  subRound( A, B, C, D, E, f4, K4, expand( data, 65 ) );
  subRound( E, A, B, C, D, f4, K4, expand( data, 66 ) );
  subRound( D, E, A, B, C, f4, K4, expand( data, 67 ) );
  subRound( C, D, E, A, B, f4, K4, expand( data, 68 ) );
  subRound( B, C, D, E, A, f4, K4, expand( data, 69 ) );
  subRound( A, B, C, D, E, f4, K4, expand( data, 70 ) );
  subRound( E, A, B, C, D, f4, K4, expand( data, 71 ) );
  subRound( D, E, A, B, C, f4, K4, expand( data, 72 ) );
  subRound( C, D, E, A, B, f4, K4, expand( data, 73 ) );
  subRound( B, C, D, E, A, f4, K4, expand( data, 74 ) );
  subRound( A, B, C, D, E, f4, K4, expand( data, 75 ) );
  subRound( E, A, B, C, D, f4, K4, expand( data, 76 ) );
  subRound( D, E, A, B, C, f4, K4, expand( data, 77 ) );
  subRound( C, D, E, A, B, f4, K4, expand( data, 78 ) );
  subRound( B, C, D, E, A, f4, K4, expand( data, 79 ) );

  /* Build message digest */
  ctx->digest[0] += A;
  ctx->digest[1] += B;
  ctx->digest[2] += C;
  ctx->digest[3] += D;
  ctx->digest[4] += E;
}

#if 1

#ifndef EXTRACT_UCHAR
#define EXTRACT_UCHAR(p)  (*(unsigned char *)(p))
#endif

#define STRING2INT(s) ((((((EXTRACT_UCHAR(s) << 8)    \
			 | EXTRACT_UCHAR(s+1)) << 8)  \
			 | EXTRACT_UCHAR(s+2)) << 8)  \
			 | EXTRACT_UCHAR(s+3))
#else
uint32_t STRING2INT(unsigned char *s)
{
  uint32_t r;
  unsigned int i;
  
  for (i = 0, r = 0; i < 4; i++, s++)
    r = (r << 8) | *s;
  return r;
}
#endif

static void sha_block(struct SHA_CTX *ctx, const unsigned char *block)
{
  uint32_t data[SHA_DATALEN];
  unsigned int i;
  
  /* Update block count */
  if (!++ctx->count_l)
    ++ctx->count_h;

  /* Endian independent conversion */
  for (i = 0; i<SHA_DATALEN; i++, block += 4)
    data[i] = STRING2INT(block);

  sha_transform(ctx, data);
}

void SHA1_Update(struct SHA_CTX *ctx, const unsigned char *buffer, uint32_t len)
{
  if (ctx->index)
    { /* Try to fill partial block */
      unsigned left = SHA_DATASIZE - ctx->index;
      if (len < left)
	{
	  memcpy(ctx->block + ctx->index, buffer, len);
	  ctx->index += len;
	  return; /* Finished */
	}
      else
	{
	  memcpy(ctx->block + ctx->index, buffer, left);
	  sha_block(ctx, ctx->block);
	  buffer += left;
	  len -= left;
	}
    }
  while (len >= SHA_DATASIZE)
    {
      sha_block(ctx, buffer);
      buffer += SHA_DATASIZE;
      len -= SHA_DATASIZE;
    }
  if ((ctx->index = len))     /* This assignment is intended */
    /* Buffer leftovers */
    memcpy(ctx->block, buffer, len);
}
	  
/* Final wrapup - pad to SHA_DATASIZE-byte boundary with the bit pattern
   1 0* (64-bit count of bits processed, MSB-first) */

void SHA1_Final(unsigned char *s, struct SHA_CTX *ctx)
{
  uint32_t data[SHA_DATALEN];
  unsigned int i;
  unsigned int words;
  
  i = ctx->index;
  /* Set the first char of padding to 0x80.  This is safe since there is
     always at least one byte free */
  ctx->block[i++] = 0x80;

  /* Fill rest of word */
  for( ; i & 3; i++)
    ctx->block[i] = 0;

  /* i is now a multiple of the word size 4 */
  words = i >> 2;
  for (i = 0; i < words; i++)
    data[i] = STRING2INT(ctx->block + 4*i);
  
  if (words > (SHA_DATALEN-2))
    { /* No room for length in this block. Process it and
       * pad with another one */
      for (i = words ; i < SHA_DATALEN; i++)
	data[i] = 0;
      sha_transform(ctx, data);
      for (i = 0; i < (SHA_DATALEN-2); i++)
	data[i] = 0;
    }
  else
    for (i = words ; i < SHA_DATALEN - 2; i++)
      data[i] = 0;
  /* Theres 512 = 2^9 bits in one block */
  data[SHA_DATALEN-2] = (ctx->count_h << 9) | (ctx->count_l >> 23);
  data[SHA_DATALEN-1] = (ctx->count_l << 9) | (ctx->index << 3);
  sha_transform(ctx, data);
  sha_digest(ctx, s);
}

void sha_digest(struct SHA_CTX *ctx, unsigned char *s)
{
  unsigned int i;

  for (i = 0; i < SHA_DIGESTLEN; i++)
    {
      *s++ =         ctx->digest[i] >> 24;
      *s++ = 0xff & (ctx->digest[i] >> 16);
      *s++ = 0xff & (ctx->digest[i] >> 8);
      *s++ = 0xff &  ctx->digest[i];
    }
}
