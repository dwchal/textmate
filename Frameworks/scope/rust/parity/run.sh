#!/bin/sh
# Differential test: original C++ selector code (from git) vs the Rust port.
# Also builds and runs the framework's unit tests (Frameworks/scope/tests)
# against both implementations.
#
# Usage: Frameworks/scope/rust/parity/run.sh [baseline-git-ref] [seed]
set -e
cd "$(git rev-parse --show-toplevel)"
ref="${1:-186f7bfc}"  # last commit with the C++ selector implementation
seed="${2:-1}"
work="${TMPDIR:-/tmp}/scope-parity.$$"
mkdir -p "$work/legacy/scope" "$work/rust/scope" "$work/inc"
trap 'rm -rf "$work"' EXIT
export PATH="$HOME/.cargo/bin:$PATH"

for f in $(git ls-tree --name-only "$ref" Frameworks/scope/src/); do
	git show "$ref:$f" > "$work/legacy/scope/$(basename "$f")"
done
cp Frameworks/scope/src/* "$work/rust/scope/"
ln -s "$PWD/Frameworks/text/src" "$work/inc/text"

cargo build --quiet --release --manifest-path Frameworks/scope/rust/Cargo.toml --target-dir "$work/target"
CXX="xcrun clang++ -std=c++2a -funsigned-char -O2 -DNDEBUG -D'NULL_STR=\"\\uFFFF\"' -IShared/include -I/opt/homebrew/include -include Shared/PCH/prelude.cc -I$work/inc -w"
TEXT_SRC="Frameworks/text/src/*.cc"

build () { # name, extra objects...
	name="$1"; shift
	eval $CXX -I"$work/$name" "$work/$name/scope/*.cc" $TEXT_SRC "$@" -liconv -framework CoreFoundation -framework Foundation
}

ruby -W0 bin/gen_test Frameworks/scope/tests/*.cc > "$work/tests.cc"
for impl in legacy rust; do
	extra=""; [ $impl = rust ] && extra="$work/target/release/libscope_rs.a"
	build $impl Frameworks/scope/rust/parity/driver.cc $extra -o "$work/driver-$impl"
	build $impl "$work/tests.cc" $extra -o "$work/tests-$impl"
	printf 'unit tests (%s): ' $impl; "$work/tests-$impl" > "$work/tests-$impl.log" 2>&1 && echo pass || { echo FAIL; cat "$work/tests-$impl.log"; exit 1; }
done

python3 Frameworks/scope/rust/parity/corpus.py "$seed" \
	"$HOME/Library/Application Support/TextMate/Managed/Bundles" \
	"$HOME/Library/Application Support/TextMate/Bundles" > "$work/corpus.tsv"
"$work/driver-legacy" < "$work/corpus.tsv" > "$work/legacy.out"
"$work/driver-rust"   < "$work/corpus.tsv" > "$work/rust.out"

lines=$(wc -l < "$work/corpus.tsv" | tr -d ' ')
matches=$(grep -vc '	-$' "$work/legacy.out" || true)
if cmp -s "$work/legacy.out" "$work/rust.out"; then
	echo "parity: $lines cases identical ($matches matched), baseline $ref, seed $seed"
else
	echo "parity: MISMATCH (baseline $ref, seed $seed)"
	paste "$work/corpus.tsv" "$work/legacy.out" "$work/rust.out" | awk -F'\t' '$4"\t"$5 != $6"\t"$7' | head -20
	exit 1
fi

# Rough speed comparison on the same corpus
for impl in legacy rust; do
	/usr/bin/time -p "$work/driver-$impl" < "$work/corpus.tsv" 2>&1 >/dev/null | awk -v i=$impl '/^user/ { print "time (" i "): " $2 "s user" }'
done
