/* ECDSA verification through corecrypto against OpenSSL-generated signatures,
 * including a corrupted signature and the elapsed time of each verification. */
#include <corecrypto/ccec.h>
#include <corecrypto/ccn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static size_t slurp(const char* dir, const char* curve, const char* ext, uint8_t* buffer, size_t size)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s.%s", dir, curve, ext);
	FILE* file = fopen(path, "rb");
	if (!file) return 0;
	size_t length = fread(buffer, 1, size, file);
	fclose(file);
	return length;
}

static int check(const char* dir, const char* curve, ccec_const_cp_t cp)
{
	uint8_t point[256], digest[64], signature[256];
	size_t point_length = slurp(dir, curve, "pt", point, sizeof(point));
	size_t digest_length = slurp(dir, curve, "dig", digest, sizeof(digest));
	size_t signature_length = slurp(dir, curve, "sig", signature, sizeof(signature));
	if (!point_length || !digest_length || !signature_length) { printf("ECDSA %s missing-input FAIL\n", curve); return 1; }

	ccec_pub_ctx_decl_cp(cp, key);
	int imported = ccec_import_pub(cp, point_length, point, key);
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC, &start);
	bool valid = false;
	int status = ccec_verify(key, digest_length, digest, signature_length, signature, &valid);
	clock_gettime(CLOCK_MONOTONIC, &end);
	double good_seconds = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

	signature[signature_length - 3] ^= 0x40;
	bool tampered_valid = true;
	int tampered_status = ccec_verify(key, digest_length, digest, signature_length, signature, &tampered_valid);
	int ok = imported == 0 && status == 0 && valid && !(tampered_status == 0 && tampered_valid);
	printf("ECDSA %s import=%d verify=%d valid=%d tampered_rejected=%d seconds=%.3f %s\n", curve, imported, status, valid,
	       !(tampered_status == 0 && tampered_valid), good_seconds, ok ? "ok" : "FAIL");
	fflush(stdout);
	return !ok;
}

int main(int argc, char** argv)
{
	const char* dir = argc > 1 ? argv[1] : "/tmp";
	return check(dir, "prime256v1", ccec_cp_256()) | check(dir, "secp384r1", ccec_cp_384()) | check(dir, "secp521r1", ccec_cp_521());
}
