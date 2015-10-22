/* digest_cng.c
* Author: Pragma Systems, Inc. <www.pragmasys.com>
* Contribution by Pragma Systems, Inc. for Microsoft openssh win32 port
* Copyright (c) 2011, 2015 Pragma Systems, Inc.
* All rights reserved
*
* Common library for Windows Console Screen IO.
* Contains Windows console related definition so that emulation code can draw
* on Windows console screen surface.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice.
* 2. Binaries produced provide no direct or implied warranties or any
*    guarantee of performance or suitability.
*/

typedef unsigned int u_int;
typedef unsigned char u_char;


#include <limits.h>
#include <string.h>

#include <digest.h>
#include <ssherr.h>

#include <Windows.h>
#include <bcrypt.h>


const u_char *sshbuf_ptr(const struct sshbuf *buf);
size_t	sshbuf_len(const struct sshbuf *buf);



struct ssh_digest_ctx {
	int alg;
	BCRYPT_ALG_HANDLE cng_alg_handle;
	BCRYPT_HASH_HANDLE hash_handle;
};

struct ssh_digest {
	int id;
	const char *name;
	size_t digest_len;
	const wchar_t * cng_alg_name;
};

/* NB. Indexed directly by algorithm number */
const struct ssh_digest digests[] = {
	{ SSH_DIGEST_MD5, "MD5", 16, BCRYPT_MD5_ALGORITHM },
	{ SSH_DIGEST_RIPEMD160,	"RIPEMD160",	20,	NULL },  /* not supported */	
	{ SSH_DIGEST_SHA1, "SHA1", 20, BCRYPT_SHA1_ALGORITHM },
	{ SSH_DIGEST_SHA256, "SHA256", 32, BCRYPT_SHA256_ALGORITHM },
	{ SSH_DIGEST_SHA384, "SHA384", 48, BCRYPT_SHA384_ALGORITHM },
	{ SSH_DIGEST_SHA512, "SHA512", 64, BCRYPT_SHA512_ALGORITHM },
	{ -1, NULL, 0, NULL },
};

static const struct ssh_digest *
ssh_digest_by_alg(int alg)
{
	if (alg < 0 || alg >= SSH_DIGEST_MAX)
		return NULL;
	if (digests[alg].id != alg) /* sanity */
		return NULL;
	if (digests[alg].cng_alg_name == NULL)
		return NULL;
	return &(digests[alg]);
}

int
ssh_digest_alg_by_name(const char *name)
{
	int alg;

	for (alg = 0; digests[alg].id != -1; alg++) {
		if (_stricmp(name, digests[alg].name) == 0)
			return digests[alg].id;
	}
	return -1;
}

const char *
ssh_digest_alg_name(int alg)
{
	const struct ssh_digest *digest = ssh_digest_by_alg(alg);

	return digest == NULL ? NULL : digest->name;
}

size_t
ssh_digest_bytes(int alg)
{
	const struct ssh_digest *digest = ssh_digest_by_alg(alg);

	return digest == NULL ? 0 : digest->digest_len;
}

size_t
ssh_digest_blocksize(struct ssh_digest_ctx *ctx)
{
	HRESULT hr = S_OK;
	DWORD blocksize = 0;
	DWORD count;

	hr = BCryptGetProperty(ctx->cng_alg_handle, BCRYPT_HASH_BLOCK_LENGTH, (PUCHAR)&blocksize, sizeof(DWORD), &count, 0);

	return (size_t)blocksize;
}

struct ssh_digest_ctx *
	ssh_digest_start(int alg)
{
	const struct ssh_digest *digest = ssh_digest_by_alg(alg);
	struct ssh_digest_ctx *ret;
	HRESULT hr = S_OK;

	if (digest == NULL || ((ret = (struct ssh_digest_ctx *)malloc(sizeof(*ret))) == NULL))
		return NULL;
	ret->alg = alg;

	if ((hr = BCryptOpenAlgorithmProvider(&(ret->cng_alg_handle), digest->cng_alg_name, NULL, 0)) != S_OK){
		free(ret);
		return NULL;
	}

	if ((hr = BCryptCreateHash(ret->cng_alg_handle, &(ret->hash_handle), NULL, 0, NULL, 0, BCRYPT_HASH_REUSABLE_FLAG)) != S_OK)
	{
		BCryptCloseAlgorithmProvider(ret->cng_alg_handle, 0);
		free(ret);
		return NULL;

	}

	return ret;
}

int
ssh_digest_copy_state(struct ssh_digest_ctx *from, struct ssh_digest_ctx *to)
{
	HRESULT hr = S_OK;

	if (from->alg != to->alg)
		return SSH_ERR_INVALID_ARGUMENT;
	if ((hr = BCryptDuplicateHash(from->hash_handle, &(to->hash_handle),NULL,0,0)) != S_OK)
		return SSH_ERR_LIBCRYPTO_ERROR;
	return 0;
}

int
ssh_digest_update(struct ssh_digest_ctx *ctx, const void *m, size_t mlen)
{
	HRESULT hr = S_OK;
	if ((hr = BCryptHashData(ctx->hash_handle, (PUCHAR)m, mlen, 0)) != S_OK)
		return SSH_ERR_LIBCRYPTO_ERROR;
	return 0;
}

int
ssh_digest_update_buffer(struct ssh_digest_ctx *ctx, const struct sshbuf *b)
{
	return ssh_digest_update(ctx, sshbuf_ptr(b), sshbuf_len(b));
}

int
ssh_digest_final(struct ssh_digest_ctx *ctx, u_char *d, size_t dlen)
{
	const struct ssh_digest *digest = ssh_digest_by_alg(ctx->alg);
	u_int l = dlen;
	HRESULT hr = S_OK;

	if (dlen > UINT_MAX)
		return SSH_ERR_INVALID_ARGUMENT;
	if (dlen < digest->digest_len) /* No truncation allowed */
		return SSH_ERR_INVALID_ARGUMENT;
	if ((hr = BCryptFinishHash(ctx->hash_handle, d, digest->digest_len, 0)) != S_OK)
		return SSH_ERR_LIBCRYPTO_ERROR;
	return 0;
}

void
ssh_digest_free(struct ssh_digest_ctx *ctx)
{
	if (ctx != NULL) {
		BCryptCloseAlgorithmProvider(ctx->cng_alg_handle, 0);
		BCryptDestroyHash(ctx->hash_handle);
		explicit_bzero(ctx, sizeof(*ctx));
		free(ctx);
	}
}

int
ssh_digest_memory(int alg, const void *m, size_t mlen, u_char *d, size_t dlen)
{
	const struct ssh_digest *digest = ssh_digest_by_alg(alg);
	struct ssh_digest_ctx *ctx = ssh_digest_start(alg);
	u_int mdlen;

	if (digest == NULL)
		return SSH_ERR_INVALID_ARGUMENT;
	if (dlen > UINT_MAX)
		return SSH_ERR_INVALID_ARGUMENT;
	if (dlen < digest->digest_len)
		return SSH_ERR_INVALID_ARGUMENT;
	mdlen = dlen;
	if (ssh_digest_update(ctx, m, mlen) != 0 ||
		ssh_digest_final(ctx, d, dlen) != 0)
		 return -1;
	ssh_digest_free(ctx);
	return 0;
}

const u_char *sshbuf_ptr(const struct sshbuf *buf);
size_t	sshbuf_len(const struct sshbuf *buf);


int
ssh_digest_buffer(int alg, const struct sshbuf *b, u_char *d, size_t dlen)
{
	return ssh_digest_memory(alg, sshbuf_ptr(b), sshbuf_len(b), d, dlen);
	return 0;
}
