#include <stdio.h>
#include <string.h>

#include "sshinner_crypt.h"
#include "st_others.h"


RET_T encrypt_init(const char* passwd, char store_key[])
{

    /** EVP_BytesToKey() derives a key and IV from various parameters. 
      * 用来产生最初始的key&iv 
      * iv后面是要不断切换的
      */

    unsigned char iv[RC4_MD5_IV_LEN];

    EVP_CIPHER * cipher = (EVP_CIPHER *)EVP_get_cipherbyname("rc4");
    const EVP_MD *md = EVP_get_digestbyname("MD5");

    if (! EVP_BytesToKey(cipher, md, NULL, passwd, strlen(passwd), 10, store_key, iv))
    {
        st_d_error("Generate key & iv failed!");
        return RET_NO;
    }

    st_d_print("初始化OK: %s", passwd);

    return RET_YES;
}


RET_T encrypt_ctx_init(P_ENCRYPT_CTX p_ctx, unsigned long salt, const char* enc_key, int enc)
{
    EVP_CIPHER_CTX_init(&p_ctx->ctx); 

    if (!EVP_CipherInit_ex(&p_ctx->ctx, EVP_rc4(), NULL, NULL, NULL, enc)) 
    {
        st_d_error("Cannot initialize cipher");
        return RET_NO;
    }

    if (!EVP_CIPHER_CTX_set_key_length(&p_ctx->ctx, RC4_MD5_KEY_LEN)) 
    {
        EVP_CIPHER_CTX_cleanup(&p_ctx->ctx);
        st_d_error("Invalid key length: %d", RC4_MD5_KEY_LEN);
        return RET_NO;
    }

    //OPENSSL_assert(EVP_CIPHER_CTX_key_length(&p_ctx->ctx) == 16);
    //OPENSSL_assert(EVP_CIPHER_CTX_iv_length(&p_ctx->ctx) == 16);

    memcpy(p_ctx->enc_key, enc_key, RC4_MD5_KEY_LEN);

    char buf[128];
    char md5_buf[128];
    sprintf(buf, "-XX%dXX-", salt);
    MD5(buf, strlen(buf), md5_buf);
    
    memcpy(p_ctx->iv, md5_buf, RC4_MD5_IV_LEN);

    p_ctx->is_init = 0;

    return RET_YES;
}

int encrypt_ctx_free(P_ENCRYPT_CTX p_ctx)
{
    if (p_ctx)
        EVP_CIPHER_CTX_cleanup(&p_ctx->ctx); 

    p_ctx->is_init = 0;

    return 0;
}

RET_T encrypt(P_ENCRYPT_CTX p_ctx, 
               const P_ENC_FRAME p_plain, P_ENC_FRAME p_store)
{
    memset(p_store, 0, FRAME_SIZE);

    if (!p_ctx->is_init) 
    {
        unsigned char key_iv[32]; 
        memcpy(key_iv, p_ctx->enc_key, 16); 
        memcpy(key_iv + 16, p_ctx->iv, 16);
        const unsigned char *true_key = 
                                    MD5(key_iv, 32, NULL);

        if (!EVP_CipherInit_ex(&p_ctx->ctx, NULL, NULL, true_key, p_ctx->iv, 1)) 
        {
            st_d_error("Cannot set key and IV\n");
            return RET_NO;
        }

        p_ctx->is_init = 1;
    }

    if (! EVP_CipherUpdate(&p_ctx->ctx, p_store->dat, &p_store->len, 
                            p_plain->dat, p_plain->len) )
    {
        st_d_error("Encrypt error!");
        return RET_NO;
    }

    return RET_YES;
}


RET_T decrypt(P_ENCRYPT_CTX p_ctx, 
                const P_ENC_FRAME p_cipher, P_ENC_FRAME p_store)
{
    memset(p_store, 0, FRAME_SIZE);

    if (!p_ctx->is_init) 
    {
        unsigned char key_iv[32]; 
        memcpy(key_iv, p_ctx->enc_key, 16); 
        memcpy(key_iv + 16, p_ctx->iv, 16);
        const unsigned char *true_key = 
                                    MD5(key_iv, 32, NULL);

        if (!EVP_CipherInit_ex(&p_ctx->ctx, NULL, NULL, true_key, p_ctx->iv, 0)) 
        {
            st_d_error("Cannot set key and IV\n");
            return RET_NO;
        }

        p_ctx->is_init = 1;
    }


    if(! EVP_CipherUpdate(&p_ctx->ctx, p_store->dat, &p_store->len,
                                    p_cipher->dat, p_cipher->len))
    {
        st_d_error("Decrypt error!");
        return RET_NO;
    }

    return RET_YES;
}

#if 0
int enc_test(void)
{
    ENCRYPT_CTX enc;
    ENCRYPT_CTX dec;

    encrypt_init();

    encrypt_ctx_init(&enc, 1);
    encrypt_ctx_init(&dec, 0);

    char* msg = "桃子啊 aaaaaasdfaedddd!";

    ENC_FRAME org;
    ENC_FRAME enced;
    ENC_FRAME deced;

    memset(&org, 0, FRAME_SIZE);
    strncpy(org.dat, msg, strlen(msg+1));
    org.len = strlen(msg)+1;
    encrypt(&enc, &org, &enced);

    decrypt(&dec, &enced, &deced);

    printf((char *)deced.dat);

    printf("\n");

}

#endif
