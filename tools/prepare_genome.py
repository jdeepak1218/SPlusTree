#!/usr/bin/env python3
"""Encode consecutive A/C/G/T 16-mers as little-endian uint32 keys."""

import argparse
import array
import gzip
import hashlib
import json
from pathlib import Path
import sys

SOURCE = "https://hgdownload.soe.ucsc.edu/goldenPath/hg38/chromosomes/chr22.fa.gz"
BASES = {ord(base): value for value, base in enumerate("ACGT")}
BASES.update({ord(base): value for value, base in enumerate("acgt")})


def kmers(lines):
    key = valid = 0
    for line in lines:
        if line.startswith(b">"):
            key = valid = 0
            continue
        for base in line.strip():
            code = BASES.get(base)
            if code is None:
                key = valid = 0
                continue
            key = ((key << 2) | code) & 0xFFFFFFFF
            valid += 1
            if valid >= 16:
                yield key


def self_test():
    fixtures = [b"", b"A" * 15, b"A" * 16, b"T" * 17,
                b"acgtACGTacgtACGTNN" + b"G" * 18,
                b"A" * 15 + b"N" + b"T" * 15]
    for sequence in fixtures:
        expected = []
        for i in range(len(sequence) - 15):
            window = sequence[i:i + 16].upper()
            if all(base in b"ACGT" for base in window):
                expected.append(int("".join(str(b"ACGT".index(base)) for base in window), 4))
        # FASTA line boundaries may split a valid window.
        actual = list(kmers([b">fixture\n", sequence[:7], sequence[7:]]))
        if actual != expected:
            raise RuntimeError("16-mer encoding self-test failed")
    if list(kmers([b">a", b"A" * 15, b">b", b"T" * 15])):
        raise RuntimeError("16-mer crossed a sequence boundary")
    if list(kmers([b"T" * 16])) != [0xFFFFFFFF]:
        raise RuntimeError("Full uint32 range lost")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?", type=Path)
    parser.add_argument("destination", nargs="?", type=Path)
    args = parser.parse_args()
    self_test()
    if args.source is None and args.destination is None:
        print("PASS 16-mer encoder self-tests")
        return
    if args.source is None or args.destination is None:
        parser.error("source and destination must be provided together")
    with args.source.open("rb") as source_file:
        digest = hashlib.file_digest(source_file, "sha256").hexdigest()
    count = 0
    block = array.array("I")
    if block.itemsize != 4:
        raise RuntimeError("This platform has no 4-byte unsigned int array")
    with gzip.open(args.source, "rb") as source, args.destination.open("wb") as destination:
        for key in kmers(source):
            block.append(key)
            count += 1
            if len(block) == 65536:
                if sys.byteorder != "little":
                    block.byteswap()
                block.tofile(destination)
                block = array.array("I")
        if sys.byteorder != "little":
            block.byteswap()
        block.tofile(destination)
    with args.destination.open("rb") as derived_file:
        derived_digest = hashlib.file_digest(derived_file, "sha256").hexdigest()
    manifest = {"source_url": SOURCE, "source_sha256": digest, "keys": count,
                "encoding": "forward 16-mers, A=0 C=1 G=2 T=3; case insensitive; reset on ambiguous bases and headers; little-endian uint32; sequence order",
                "derived_sha256": derived_digest}
    args.destination.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
