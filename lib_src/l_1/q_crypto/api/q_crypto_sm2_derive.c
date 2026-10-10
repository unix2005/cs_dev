/**
 * @file q_crypto_sm2_derive.c
 * @brief q_crypto_sm2_derive —— SM2 密钥协商（ECDH，基于 Tongsuo EC 原语）
 * @note  Tongsuo 的 SM2 EVP_PKEY 未注册普通 ECDH derive 操作，故采用标准 ECDH：
 *        以己方私钥 d 与对端公钥点 Q 计算共享点 S = d·Q，取其 X 坐标（32 字节）为共享密钥。
 *        曲线运算全部由 Tongsuo 的 EC 模块完成（非自研算法），双方分别用 (d_A,P_B)/(d_B,P_A)
 *        得到相同的 X 坐标。
 */
#include "headers.h"
#include "q_crypto.h"

int q_crypto_sm2_derive(const uint8_t *my_priv_der, size_t my_priv_len,
                        const uint8_t *peer_pub_der, size_t peer_pub_len,
                        uint8_t out[32])
{
    if (!my_priv_der || !peer_pub_der || !out)
        return -1;

    EVP_PKEY *priv = q_crypto_sm2_load_priv(my_priv_der, my_priv_len);
    EVP_PKEY *peer = q_crypto_sm2_load_pub(peer_pub_der, peer_pub_len);
    if (!priv || !peer)
    {
        EVP_PKEY_free(priv);
        EVP_PKEY_free(peer);
        return -1;
    }

    int rc = -1;
    /* EVP_PKEY_get1_EC_KEY 在 OpenSSL 3.x 标记为 deprecated，此处用于取出 EC 密钥参数做 ECDH */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    EC_KEY *ec_priv = EVP_PKEY_get1_EC_KEY(priv);
    EC_KEY *ec_peer = EVP_PKEY_get1_EC_KEY(peer);

    if (ec_priv && ec_peer)
    {
        const EC_GROUP *grp = EC_KEY_get0_group(ec_priv);
        const BIGNUM *d = EC_KEY_get0_private_key(ec_priv);
        const EC_POINT *Q = EC_KEY_get0_public_key(ec_peer);
        EC_POINT *S = EC_POINT_new(grp);
        if (grp && d && Q && S)
        {
            if (EC_POINT_mul(grp, S, NULL, Q, d, NULL) == 1)
            {
                BIGNUM *X = BN_new();
                if (X && EC_POINT_get_affine_coordinates(grp, S, X, NULL, NULL) == 1)
                {
                    if (BN_bn2binpad(X, out, 32) == 32)
                        rc = 0;
                }
                BN_free(X);
            }
        }
        EC_POINT_free(S);
    }
    EC_KEY_free(ec_priv);
    EC_KEY_free(ec_peer);
    EVP_PKEY_free(priv);
    EVP_PKEY_free(peer);
#pragma GCC diagnostic pop
    return rc;
}
