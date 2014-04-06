#!/bin/zsh

bsize=8192
testfile="$(mktemp)"

function makeblock() {
	for i in `seq "$bsize"`; do
		echo -n "$1"
	done
}

for sect in a b c d; do
	makeblock "$sect" >> "$testfile"
done
echo "This should be 32kb:"
ls -l "$testfile"
echo ""

if ! ./bcachify "$testfile" "$bsize"; then
	echo "fail"
	exit 1
fi

testfile2="$(mktemp)"
for sect in a a b c; do
	makeblock "$sect" >> "$testfile2"
done

echo "\nThese should be equal:"
sha1sum "$testfile" "$testfile2"

rm -f "$testfile" "$testfile2"
