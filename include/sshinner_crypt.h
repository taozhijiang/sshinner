#ifndef _SSHINNER_CRYPT_H
#define _SSHINNER_CRYPT_H

#include "st_others.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/md5.h>
#include <openssl/rand.h>

#define RC4_MD5_KEY_LEN     16
#define RC4_MD5_IV_LEN      16

#define FRAME_SIZE 4096

/*简单起见，不进行iv的传输，iv的计算根据每次连接信息计算出来*/

typedef struct _enc_frame {
    char  dat[FRAME_SIZE];
    int   len;
} ENC_FRAME, *P_ENC_FRAME;


typedef struct _encrypt_ctx{
    EVP_CIPHER_CTX  ctx;
    int             is_init;
    unsigned char   enc_key[RC4_MD5_KEY_LEN];
    unsigned char   iv[RC4_MD5_KEY_LEN];  //random everytime RAND_bytes(output, len);
} ENCRYPT_CTX, *P_ENCRYPT_CTX;

RET_T encrypt_init(const char* passwd, char store_key[]);
RET_T encrypt_ctx_init(P_ENCRYPT_CTX p_ctx, unsigned long salt, 
                       const char* enc_key, int enc);
int encrypt_ctx_free(P_ENCRYPT_CTX p_ctx);

RET_T encrypt(P_ENCRYPT_CTX p_ctx, 
               const P_ENC_FRAME p_plain, P_ENC_FRAME p_store);
RET_T decrypt(P_ENCRYPT_CTX p_ctx, 
                const P_ENC_FRAME p_cipher, P_ENC_FRAME p_store);

#endif
