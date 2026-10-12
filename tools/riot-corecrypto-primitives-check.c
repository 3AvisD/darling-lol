/* ccn_add/ccn_sub/ccn_cmp (10 units) and ccn_mul (9 units) against reference values. */
#include <corecrypto/ccn.h>
#include <corecrypto/private/ccn_extra.h>
#include <stdio.h>
#include <string.h>

static void parse(const char* hex, cc_unit* out, cc_size n)
{
	memset(out, 0, ccn_sizeof_n(n));
	size_t length = strlen(hex);
	for (size_t i = 0; i < length && i / 16 < n; ++i) {
		int c = hex[length - 1 - i];
		out[i / 16] |= (cc_unit)(c <= '9' ? c - '0' : c - 'a' + 10) << ((i % 16) * 4);
	}
}

int main(int argc, char** argv)
{
	FILE* file = fopen(argv[1], "r");
	if (!file) return 3;
	char x_hex[300], y_hex[300], s_hex[300], d_hex[300], a_hex[300], b_hex[300], p_hex[400];
	int cmp_expected, row = 0, bad_add = 0, bad_sub = 0, bad_cmp = 0, bad_mul = 0;
	while (fscanf(file, "%299s %299s %299s %299s %d %299s %299s %399s", x_hex, y_hex, s_hex, d_hex, &cmp_expected, a_hex, b_hex, p_hex) == 8) {
		cc_unit x[10], y[10], s[10], d[10], got[10], a[9], b[9], p[18], gotp[18];
		parse(x_hex, x, 10); parse(y_hex, y, 10); parse(s_hex, s, 10); parse(d_hex, d, 10);
		parse(a_hex, a, 9); parse(b_hex, b, 9); parse(p_hex, p, 18);
		ccn_add(10, got, x, y); bad_add += memcmp(got, s, sizeof got) != 0;
		ccn_sub(10, got, x, y); bad_sub += memcmp(got, d, sizeof got) != 0;
		int c = ccn_cmp(10, x, y); c = (c > 0) - (c < 0);
		bad_cmp += (c + 1) != cmp_expected;
		ccn_mul(9, gotp, a, b); bad_mul += memcmp(gotp, p, sizeof gotp) != 0;
		++row;
	}
	printf("PRIMITIVES rows=%d bad_add=%d bad_sub=%d bad_cmp=%d bad_mul=%d\n", row, bad_add, bad_sub, bad_cmp, bad_mul);
	return bad_add + bad_sub + bad_cmp + bad_mul != 0;
}
