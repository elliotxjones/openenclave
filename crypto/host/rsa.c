// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include <limits.h>
#include <openenclave/bits/rsa.h>
#include <openenclave/bits/sha.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <stdio.h>
#include <string.h>
#include "../util.h"
#include "init.h"

/*
**==============================================================================
**
** Local defintions:
**
**==============================================================================
*/

#define OE_RSA_KEY_MAGIC 0x2a11ed055e91b281

typedef struct _OE_RSA_KEY_IMPL
{
    uint64_t magic;
    RSA* rsa;
} OE_RSA_KEY_IMPL;

OE_STATIC_ASSERT(sizeof(OE_RSA_KEY_IMPL) <= sizeof(OE_RSA_KEY));

OE_INLINE void _ClearImpl(OE_RSA_KEY_IMPL* impl)
{
    if (impl)
    {
        impl->magic = 0;
        impl->rsa = NULL;
    }
}

OE_INLINE bool _ValidImpl(const OE_RSA_KEY_IMPL* impl)
{
    return impl && impl->magic == OE_RSA_KEY_MAGIC && impl->rsa ? true : false;
}

static int _MapHashType(OE_HashType md)
{
    switch (md)
    {
        case OE_HASH_TYPE_SHA256:
            return NID_sha256;
        case OE_HASH_TYPE_SHA512:
            return NID_sha512;
    }

    /* Unreachable */
    return 0;
}

/*
**==============================================================================
**
** Public defintions:
**
**==============================================================================
*/

OE_Result OE_RSAReadPrivateKeyFromPEM(
    const uint8_t* pemData,
    size_t pemSize,
    OE_RSA_KEY* key)
{
    OE_Result result = OE_UNEXPECTED;
    OE_RSA_KEY_IMPL* impl = (OE_RSA_KEY_IMPL*)key;
    BIO* bio = NULL;
    RSA* rsa = NULL;

    /* Initialize the key output parameter */
    _ClearImpl(impl);

    /* Check parameters */
    if (!pemData || pemSize == 0 || !impl)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* The position of the null terminator must be the last byte */
    if (OE_CheckForNullTerminator(pemData, pemSize) != OE_OK)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize OpenSSL */
    OE_InitializeOpenSSL();

    /* Create a BIO object for loading the PEM data */
    if (!(bio = BIO_new_mem_buf(pemData, pemSize)))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Read the RSA structure from the PEM data */
    if (!(rsa = PEM_read_bio_RSAPrivateKey(bio, &rsa, NULL, NULL)))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Set the output key parameter */
    impl->magic = OE_RSA_KEY_MAGIC;
    impl->rsa = rsa;
    rsa = NULL;

    result = OE_OK;

done:

    if (rsa)
        RSA_free(rsa);

    if (bio)
        BIO_free(bio);

    return result;
}

OE_Result OE_RSAReadPublicKeyFromPEM(
    const uint8_t* pemData,
    size_t pemSize,
    OE_RSA_KEY* key)
{
    OE_Result result = OE_UNEXPECTED;
    OE_RSA_KEY_IMPL* impl = (OE_RSA_KEY_IMPL*)key;
    BIO* bio = NULL;
    RSA* rsa = NULL;
    EVP_PKEY* pkey = NULL;

    /* Initialize the key output parameter */
    _ClearImpl(impl);

    /* Check parameters */
    if (!pemData || pemSize == 0 || !impl)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* The position of the null terminator must be the last byte */
    if (OE_CheckForNullTerminator(pemData, pemSize) != OE_OK)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize OpenSSL */
    OE_InitializeOpenSSL();

    /* Create a BIO object for loading the PEM data */
    if (!(bio = BIO_new_mem_buf(pemData, pemSize)))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Read the RSA structure from the PEM data */
    if (!(pkey = PEM_read_bio_PUBKEY(bio, &pkey, NULL, NULL)))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Get RSA key from public key without increasing reference count */
    if (!(rsa = EVP_PKEY_get1_RSA(pkey)))
        goto done;

    /* Increase reference count of RSA key */
    RSA_up_ref(rsa);

    /* Set the output key parameter */
    impl->magic = OE_RSA_KEY_MAGIC;
    impl->rsa = rsa;
    rsa = NULL;

    result = OE_OK;

done:

    if (rsa)
        RSA_free(rsa);

    if (pkey)
        EVP_PKEY_free(pkey);

    if (bio)
        BIO_free(bio);

    return result;
}

OE_Result OE_RSAFree(OE_RSA_KEY* key)
{
    OE_Result result = OE_UNEXPECTED;
    OE_RSA_KEY_IMPL* impl = (OE_RSA_KEY_IMPL*)key;

    /* Check the parameter */
    if (!_ValidImpl(impl))
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Release the RSA object */
    RSA_free(impl->rsa);

    /* Clear the fields in the implementation */
    _ClearImpl(impl);

    result = OE_OK;

done:
    return result;
}

OE_Result OE_RSASign(
    const OE_RSA_KEY* privateKey,
    OE_HashType hashType,
    const void* hashData,
    size_t hashSize,
    uint8_t* signature,
    size_t* signatureSize)
{
    OE_Result result = OE_UNEXPECTED;
    const OE_RSA_KEY_IMPL* impl = (OE_RSA_KEY_IMPL*)privateKey;
    int type = _MapHashType(hashType);

    /* Check for null parameters */
    if (!_ValidImpl(impl) || !hashData || !hashSize || !signatureSize)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* If signature buffer is null, then signature size must be zero */
    if (!signature && *signatureSize != 0)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize OpenSSL */
    OE_InitializeOpenSSL();

    /* Determine the size of the signature; fail if buffer is too small */
    {
        size_t size = RSA_size(impl->rsa);

        if (size > *signatureSize)
        {
            *signatureSize = size;
            result = OE_BUFFER_TOO_SMALL;
            goto done;
        }

        *signatureSize = size;
    }

    /* Verify that the data is signed by the given RSA private key */
    unsigned int siglen;
    if (!RSA_sign(
            type,
            hashData,
            hashSize,
            signature,
            &siglen,
            impl->rsa))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* This should never happen */
    if (siglen != *signatureSize)
    {
        result = OE_UNEXPECTED;
        goto done;
    }

    result = OE_OK;

done:

    return result;
}

OE_Result OE_RSAVerify(
    const OE_RSA_KEY* publicKey,
    OE_HashType hashType,
    const void* hashData,
    size_t hashSize,
    const uint8_t* signature,
    size_t signatureSize)
{
    OE_Result result = OE_UNEXPECTED;
    const OE_RSA_KEY_IMPL* impl = (OE_RSA_KEY_IMPL*)publicKey;
    int type = _MapHashType(hashType);

    /* Check for null parameters */
    if (!_ValidImpl(impl) || !hashSize || !hashData || !signature || signatureSize == 0)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize OpenSSL */
    OE_InitializeOpenSSL();

    /* Verify that the data is signed by the given RSA private key */
    if (!RSA_verify(
            type,
            hashData,
            hashSize,
            signature,
            signatureSize,
            impl->rsa))
    {
        result = OE_FAILURE;
        goto done;
    }

    result = OE_OK;

done:

    return result;
}

OE_Result OE_RSAGenerate(
    uint64_t bits,
    uint64_t exponent,
    OE_RSA_KEY* privateKey,
    OE_RSA_KEY* publicKey)
{
    OE_Result result = OE_UNEXPECTED;
    OE_RSA_KEY_IMPL* privateImpl = (OE_RSA_KEY_IMPL*)privateKey;
    OE_RSA_KEY_IMPL* publicImpl = (OE_RSA_KEY_IMPL*)publicKey;
    RSA* rsa = NULL;
    BIO* bio = NULL;
    EVP_PKEY* pkey = NULL;
    const char nullTerminator = '\0';

    _ClearImpl(privateImpl);
    _ClearImpl(publicImpl);

    /* Check parameters */
    if (!privateImpl || !publicImpl)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Check range of bits parameter */
    if (bits > INT_MAX)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Check range of exponent parameter */
    if (exponent > ULONG_MAX)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Initialize OpenSSL */
    OE_InitializeOpenSSL();

    /* Generate an RSA key pair */
    if (!(rsa = RSA_generate_key(bits, exponent, 0, 0)))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Create private key object */
    {
        BUF_MEM* mem;

        if (!(bio = BIO_new(BIO_s_mem())))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (!PEM_write_bio_RSAPrivateKey(bio, rsa, NULL, NULL, 0, NULL, NULL))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (BIO_write(bio, &nullTerminator, sizeof(nullTerminator)) <= 0)
        {
            result = OE_FAILURE;
            goto done;
        }

        if (!BIO_get_mem_ptr(bio, &mem))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (OE_RSAReadPrivateKeyFromPEM(
                (uint8_t*)mem->data, mem->length, privateKey) != OE_OK)
        {
            result = OE_FAILURE;
            goto done;
        }

        BIO_free(bio);
        bio = NULL;
    }

    /* Create public key object */
    {
        BUF_MEM* mem;

        if (!(bio = BIO_new(BIO_s_mem())))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (!(pkey = EVP_PKEY_new()))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (!(EVP_PKEY_assign_RSA(pkey, rsa)))
        {
            result = OE_FAILURE;
            goto done;
        }

        rsa = NULL;

        if (!PEM_write_bio_PUBKEY(bio, pkey))
        {
            result = OE_FAILURE;
            goto done;
        }

        if (BIO_write(bio, &nullTerminator, sizeof(nullTerminator)) <= 0)
        {
            result = OE_FAILURE;
            goto done;
        }

        BIO_get_mem_ptr(bio, &mem);

        if (OE_RSAReadPublicKeyFromPEM(
                (uint8_t*)mem->data, mem->length, publicKey) != OE_OK)
        {
            result = OE_FAILURE;
            goto done;
        }

        BIO_free(bio);
        bio = NULL;
    }

    result = OE_OK;

done:

    if (rsa)
        RSA_free(rsa);

    if (bio)
        BIO_free(bio);

    if (pkey)
        EVP_PKEY_free(pkey);

    if (result != OE_OK)
    {
        if (_ValidImpl(privateImpl))
            OE_RSAFree(privateKey);

        if (_ValidImpl(publicImpl))
            OE_RSAFree(publicKey);
    }

    return result;
}

OE_Result OE_RSAWritePrivateKeyToPEM(
    const OE_RSA_KEY* key,
    uint8_t* data,
    size_t* size)
{
    OE_Result result = OE_UNEXPECTED;
    const OE_RSA_KEY_IMPL* impl = (const OE_RSA_KEY_IMPL*)key;
    BIO* bio = NULL;
    const char nullTerminator = '\0';

    /* Check parameters */
    if (!_ValidImpl(impl) || !size)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* If buffer is null, then size must be zero */
    if (!data && *size != 0)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Create memory BIO to write to */
    if (!(bio = BIO_new(BIO_s_mem())))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Write key to the BIO */
    if (!PEM_write_bio_RSAPrivateKey(bio, impl->rsa, NULL, NULL, 0, NULL, NULL))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Write a null terminator onto the BIO */
    if (BIO_write(bio, &nullTerminator, sizeof(nullTerminator)) <= 0)
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Copy the BIO onto caller's memory */
    {
        BUF_MEM* mem;

        if (!BIO_get_mem_ptr(bio, &mem))
        {
            result = OE_FAILURE;
            goto done;
        }

        /* If buffer is too small */
        if (*size < mem->length)
        {
            *size = mem->length;
            result = OE_BUFFER_TOO_SMALL;
            goto done;
        }

        /* Copy buffer onto caller's memory */
        memcpy(data, mem->data, mem->length);
        *size = mem->length;
    }

    result = OE_OK;

done:

    if (bio)
        BIO_free(bio);

    return result;
}

OE_Result OE_RSAWritePublicKeyToPEM(
    const OE_RSA_KEY* key,
    uint8_t* data,
    size_t* size)
{
    const OE_RSA_KEY_IMPL* impl = (const OE_RSA_KEY_IMPL*)key;
    OE_Result result = OE_UNEXPECTED;
    BIO* bio = NULL;
    EVP_PKEY* pkey = NULL;
    const char nullTerminator = '\0';

    /* Check parameters */
    if (!_ValidImpl(impl) || !size)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* If buffer is null, then size must be zero */
    if (!data && *size != 0)
    {
        result = OE_INVALID_PARAMETER;
        goto done;
    }

    /* Create memory BIO object to write key to */
    if (!(bio = BIO_new(BIO_s_mem())))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Create PKEY wrapper structure */
    if (!(pkey = EVP_PKEY_new()))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Assign key into PKEY wrapper structure */
    {
        if (!(EVP_PKEY_assign_RSA(pkey, impl->rsa)))
        {
            result = OE_FAILURE;
            goto done;
        }

        RSA_up_ref(impl->rsa);
    }

    /* Write key to BIO */
    if (!PEM_write_bio_PUBKEY(bio, pkey))
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Write a NULL terminator onto BIO */
    if (BIO_write(bio, &nullTerminator, sizeof(nullTerminator)) <= 0)
    {
        result = OE_FAILURE;
        goto done;
    }

    /* Copy the BIO onto caller's memory */
    {
        BUF_MEM* mem;

        if (!BIO_get_mem_ptr(bio, &mem))
        {
            result = OE_FAILURE;
            goto done;
        }

        /* If buffer is too small */
        if (*size < mem->length)
        {
            *size = mem->length;
            result = OE_BUFFER_TOO_SMALL;
            goto done;
        }

        /* Copy buffer onto caller's memory */
        memcpy(data, mem->data, mem->length);
        *size = mem->length;
    }

    result = OE_OK;

done:

    if (bio)
        BIO_free(bio);

    if (pkey)
        EVP_PKEY_free(pkey);

    return result;
}
