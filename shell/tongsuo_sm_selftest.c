/* Tongsuo Windows 版功能自检：SM3 / SM4-GCM / SM2（对应 q_crypto 实际用到的三组原语） */
#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/obj_mac.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

static void hexdump(const char *tag, const unsigned char *b, int n) {
    printf("%s: ", tag);
    for (int i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

/* ---------- 1) SM3 ---------- */
static int test_sm3(void) {
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    const EVP_MD *md = EVP_get_digestbyname("SM3");
    unsigned char out[32];
    unsigned int len = 0;
    if (!md) { printf("SM3: 未找到算法\n"); return 0; }
    EVP_DigestInit_ex(c, md, NULL);
    EVP_DigestUpdate(c, "abc", 3);
    EVP_DigestFinal_ex(c, out, &len);
    EVP_MD_CTX_free(c);
    hexdump("SM3(\"abc\")", out, len);
    /* 标准向量 66c7f0f4... */
    static const unsigned char ref[32] = {
        0x66,0xc7,0xf0,0xf4,0x62,0xee,0xed,0xd9,0xd1,0xf2,0xd4,0x6b,0xdc,0x10,0xe4,0xe2,
        0x41,0x67,0xc4,0x87,0x5c,0xf2,0xf7,0xa2,0x29,0x7d,0xa0,0x2b,0x8f,0x4b,0xa8,0xe0 };
    int ok = (len == 32 && memcmp(out, ref, 32) == 0);
    printf("SM3: %s\n", ok ? "OK（与标准向量一致）" : "FAIL");
    return ok;
}

/* ---------- 2) SM4-GCM ---------- */
static int test_sm4_gcm(void) {
    const unsigned char key[16] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
    const unsigned char iv[12] = {0};
    const unsigned char pt[] = "SM4-GCM self test payload";
    unsigned char ct[128], rt[128], tag[16];
    int cl = 0, rl = 0, ok = 0;

    EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
    const EVP_CIPHER *ci = EVP_get_cipherbyname("SM4-GCM");
    if (!ci) { printf("SM4-GCM: 未找到算法\n"); return 0; }

    EVP_EncryptInit_ex(c, ci, NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_EncryptInit_ex(c, NULL, NULL, key, iv);
    EVP_EncryptUpdate(c, ct, &cl, pt, (int)sizeof(pt) - 1);
    EVP_EncryptFinal_ex(c, ct + cl, &rl);
    cl += rl;
    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_GET_TAG, 16, tag);
    EVP_CIPHER_CTX_free(c);
    hexdump("SM4-GCM 密文", ct, cl);
    hexdump("SM4-GCM tag", tag, 16);

    c = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(c, ci, NULL, NULL, NULL);
    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_IVLEN, 12, NULL);
    EVP_DecryptInit_ex(c, NULL, NULL, key, iv);
    EVP_DecryptUpdate(c, rt, &rl, ct, cl);
    int tl = rl;
    EVP_CIPHER_CTX_ctrl(c, EVP_CTRL_GCM_SET_TAG, 16, tag);
    ok = EVP_DecryptFinal_ex(c, rt + tl, &rl);
    EVP_CIPHER_CTX_free(c);
    if (ok > 0) {
        rt[tl] = 0;
        printf("SM4-GCM 解密: \"%s\" -> %s\n", rt,
               memcmp(rt, pt, sizeof(pt) - 1) == 0 ? "OK" : "FAIL");
    } else {
        printf("SM4-GCM 解密: FAIL（tag 校验未通过）\n");
    }
    return ok > 0;
}

/* ---------- 3) SM2 密钥生成 + 签名/验签 ---------- */
static int test_sm2(void) {
    EVP_PKEY_CTX *kctx = EVP_PKEY_CTX_new_id(EVP_PKEY_SM2, NULL);
    EVP_PKEY *pkey = NULL;
    EVP_MD_CTX *mctx = NULL;
    unsigned char sig[256];
    size_t siglen = sizeof(sig);
    const char *msg = "SM2 sign test";
    int ok = 0;

    if (!kctx) { printf("SM2: EVP_PKEY_SM2 不可用\n"); return 0; }
    EVP_PKEY_keygen_init(kctx);
    if (EVP_PKEY_keygen(kctx, &pkey) <= 0) { printf("SM2: 密钥生成失败\n"); goto done; }
    printf("SM2: 密钥对生成 OK（bits=%d）\n", EVP_PKEY_get_bits(pkey));

    /* 签名：必须以摘要名 "SM3" 经 _ex 初始化（q_crypto 里同样这么写） */
    mctx = EVP_MD_CTX_new();
    if (EVP_DigestSignInit_ex(mctx, NULL, "SM3", NULL, NULL, pkey, NULL) <= 0) {
        printf("SM2: DigestSignInit 失败\n"); goto done;
    }
    if (EVP_DigestSign(mctx, sig, &siglen, (const unsigned char *)msg, strlen(msg)) <= 0) {
        printf("SM2: 签名失败\n"); goto done;
    }
    printf("SM2: 签名 OK（%zu 字节）\n", siglen);

    mctx = EVP_MD_CTX_new();
    if (EVP_DigestVerifyInit_ex(mctx, NULL, "SM3", NULL, NULL, pkey, NULL) <= 0) {
        printf("SM2: DigestVerifyInit 失败\n"); goto done;
    }
    int r = EVP_DigestVerify(mctx, sig, siglen, (const unsigned char *)msg, strlen(msg));
    printf("SM2: 验签 %s\n", r == 1 ? "OK" : "FAIL");
    ok = (r == 1);
done:
    EVP_MD_CTX_free(mctx);
    EVP_PKEY_free(pkey);
    EVP_PKEY_CTX_free(kctx);
    return ok;
}

int main(void) {
    printf("Tongsuo: %s\n", OpenSSL_version(OPENSSL_VERSION_STRING));
    printf("===========================================\n");
    int a = test_sm3();
    int b = test_sm4_gcm();
    int c = test_sm2();
    printf("===========================================\n");
    printf("汇总: SM3=%s SM4-GCM=%s SM2=%s\n",
           a ? "OK" : "FAIL", b ? "OK" : "FAIL", c ? "OK" : "FAIL");
    return (a && b && c) ? 0 : 1;
}
