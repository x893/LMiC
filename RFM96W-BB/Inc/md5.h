#ifndef __MD5_H__
#define __MD5_H__

#include <stdint.h>

/* Data structure for MD5 (Message-Digest) computation */
typedef struct {
	uint32_t	i[2];		/* number of _bits_ handled mod 2^64 */
	uint32_t	buf[4];		/* scratch buffer */
	uint8_t		in[64];		/* input buffer */
	uint8_t		digest[16];	/* actual digest after MD5Final call */
} MD5_CTX;

void MD5_Init	(void);
void MD5_Update	(const uint8_t * inBuf, uint8_t inLen);
void MD5_Final	(uint8_t * hash);

#endif /* __MD5_H__ */
