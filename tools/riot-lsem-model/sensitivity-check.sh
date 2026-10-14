#!/bin/sh
# Prove the model is not vacuous: re-break the fixed protocol in a copy of the extracted code, build the same model
# against it and show that the counters catch the bug. Nothing here touches the tree or the shipped library.
# Usage: sh sensitivity-check.sh   (after run.sh; writes the outputs to build/riot-lsem-model)
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$(cd "$here/../.." && pwd)/build/riot-lsem-model
tmp=${TMPDIR:-/tmp}/lsem-sensitivity.$$
mkdir -p "$tmp"

make_variant() { # $1 = variant name, $2 = python replacement program
	mkdir -p "$tmp/$1"
	cp "$here/lsem-model.c" "$tmp/$1/"
	python3 - "$out/lsem-shipped.inc" "$tmp/$1/lsem-shipped.inc" <<EOF
import sys
from pathlib import Path
src = Path(sys.argv[1]).read_text()
$2
Path(sys.argv[2]).write_text(src)
EOF
	cc -O2 -pthread -o "$tmp/$1/lsem-model" "$tmp/$1/lsem-model.c"
	(cd "$tmp/$1" && ./lsem-model) > "$out/sensitivity-$1-output.txt" 2>&1 || true
	grep -E "FAIL|LSEM_MODEL checks" "$out/sensitivity-$1-output.txt" || true
}

echo "== broken tombstone: the destroy's wake value is a positive count again and the wait may take it"
make_variant broken-tombstone '
src = src.replace("(((old >> 32) + 1) << 32) | (unsigned int) LSEM_TOMB_VALUE",
                  "(((old >> 32) + 1) << 32) | 0x40000000u")
src = src.replace("""		if (v < 0) {
			/* the destroy tombstone: a trap that resolved the slot while the destroy was in flight must
			 * report the death, never take the tombstone for a token */
			(void) lsem_post_signal(post);
			return KERN_TERMINATED;
		}
""", "")
'

echo "== broken reuse: no new generation when a slot is reassigned and a value-only (32-bit) token CAS"
make_variant broken-reuse '
src = src.replace("unsigned long long fresh = (((old >> 32) + 1) << 32)", "unsigned long long fresh = ((old >> 32) << 32)")
src = src.replace("""		if ((unsigned int) __atomic_load_n(&s->pv.w.gen, __ATOMIC_ACQUIRE) != gen ||
		    __atomic_load_n(&s->name, __ATOMIC_ACQUIRE) != name) {
			(void) lsem_post_signal(post); /* XNU signals the signal semaphore even when the wait fails */""",
"""		if ((unsigned int) __atomic_load_n(&s->pv.w.gen, __ATOMIC_ACQUIRE) != gen) {
			(void) lsem_post_signal(post); /* XNU signals the signal semaphore even when the wait fails */""")
src = src.replace("""		unsigned long long old = __atomic_load_n(&s->pv.pair, __ATOMIC_SEQ_CST);
		if ((unsigned int) (old >> 32) != gen)
			continue; /* the slot changed while we looked at it: the top of the loop reports it */
		int v = (int) (unsigned int) old;
		if (v < 0) {
			/* the destroy tombstone: a trap that resolved the slot while the destroy was in flight must
			 * report the death, never take the tombstone for a token */
			(void) lsem_post_signal(post);
			return KERN_TERMINATED;
		}
		if (v > 0) {
			/* take the token only if value and generation still belong to this object: the compare-exchange
			 * is on the whole pair, so a reused slot cannot hand its token to this waiter */
			if (__atomic_compare_exchange_n(&s->pv.pair, &old, old - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {""",
"""		int v = (int) (unsigned int) __atomic_load_n(&s->pv.pair, __ATOMIC_SEQ_CST);
		if (v > 0) {
			if (__atomic_compare_exchange_n((int*) &s->pv.w.value, &v, v - 1, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {""")
'

rm -rf "$tmp"
echo "== done; outputs are $out/sensitivity-broken-*-output.txt"
