/* Modular arithmetic on the P-521 group order against reference values. */
#include <corecrypto/ccec.h>
#include <corecrypto/cczp.h>
#include <corecrypto/ccn.h>
#include <corecrypto/private/ccn_extra.h>
#include <corecrypto/private/cczp_extra.h>
#include <corecrypto/private/ccec_extra.h>
#include <stdio.h>
#include <string.h>

static void parse(const char* hex, cc_unit* out, cc_size n)
{
	memset(out, 0, ccn_sizeof_n(n));
	size_t length = strlen(hex);
	for (size_t i = 0; i < length; ++i) {
		int digit = hex[length - 1 - i] <= '9' ? hex[length - 1 - i] - '0' : hex[length - 1 - i] - 'a' + 10;
		out[i / 16] |= (cc_unit)digit << ((i % 16) * 4);
	}
}

int main(int argc, char** argv)
{
	FILE* file = fopen(argc > 1 ? argv[1] : "/tmp/arith.txt", "r");
	if (!file) return 3;
	const cc_size n = 9;
	ccec_const_cp_t cp = ccec_cp_521();
	const cc_unit* order = ccec_cp_o(cp);
	uint8_t zp_bytes[cczp_size(ccn_sizeof_n(n))];
	memset(zp_bytes, 0, sizeof zp_bytes);
	cczp_t order_zp = (cczp_t)(struct cczp*)zp_bytes;
	CCZP_N(order_zp) = n;
	memcpy(CCZP_PRIME(order_zp), order, ccn_sizeof_n((2 * n) + 1));
	order_zp.zp->mod_prime = cczp_mod;
	printf("TABLE order[0]=%016llx order[8]=%016llx recip[9..18]:", (unsigned long long)order[0], (unsigned long long)order[8]);
	for (int i = 9; i < 19; ++i) printf(" %016llx", (unsigned long long)order[i]);
	printf("\n");
	printf("COPY  recip(zp):");
	for (int i = 0; i < 10; ++i) printf(" %016llx", (unsigned long long)cczp_recip(order_zp)[i]);
	printf("\n"); fflush(stdout);
	char a_hex[200], b_hex[200], product_hex[200], inverse_hex[200];
	int index = 0, failures = 0;
	while (fscanf(file, "%199s %199s %199s %199s", a_hex, b_hex, product_hex, inverse_hex) == 4) {
		cc_unit a[n], b[n], product[n], inverse[n], got_product[n], got_inverse[n];
		parse(a_hex, a, n); parse(b_hex, b, n); parse(product_hex, product, n); parse(inverse_hex, inverse, n);
		memset(got_product, 0, sizeof got_product); memset(got_inverse, 0, sizeof got_inverse);
		cczp_mul_mod(order_zp, got_product, a, b);
		printf("MODULAR case=%d mul_mod=%d (inverse next)\n", index, memcmp(got_product, product, sizeof got_product) == 0);
		fflush(stdout);
		ccn_modular_multiplicative_inverse(n, got_inverse, a, order);
		int product_ok = memcmp(got_product, product, sizeof got_product) == 0;
		int inverse_ok = memcmp(got_inverse, inverse, sizeof got_inverse) == 0;
		printf("MODULAR case=%d mul_mod=%d inverse=%d\n", index++, product_ok, inverse_ok);
		failures += !product_ok + !inverse_ok;
	}
	printf("MODULAR_RESULT failures=%d\n", failures);
	return failures != 0;
}
