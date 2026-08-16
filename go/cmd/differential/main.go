// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Command differential checks this implementation against the cases generated
// by tools/gen_differential_cases.py, line by line.
//
//	python3 tools/gen_differential_cases.py /tmp/cases
//	go run ./cmd/differential /tmp/cases/inputs.txt /tmp/cases/expected.txt
//
// The shared vectors pin a few dozen agreed answers; this pins thousands,
// generated from the shapes the encoder's branches actually turn on.
package main

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"fmt"
	"os"

	base85n "github.com/keywan-ghadami/base85n/go"
)

func main() {
	if len(os.Args) != 3 {
		fmt.Fprintln(os.Stderr, "usage: differential <inputs.txt> <expected.txt>")
		os.Exit(2)
	}
	fi, err := os.Open(os.Args[1])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	defer fi.Close()
	fe, err := os.Open(os.Args[2])
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(2)
	}
	defer fe.Close()

	in := bufio.NewScanner(fi)
	in.Buffer(make([]byte, 1<<20), 1<<20)
	exp := bufio.NewScanner(fe)
	exp.Buffer(make([]byte, 1<<20), 1<<20)

	line, mismatches := 0, 0
	for in.Scan() && exp.Scan() {
		line++
		data, err := hex.DecodeString(in.Text())
		if err != nil {
			fmt.Fprintf(os.Stderr, "line %d: %v\n", line, err)
			os.Exit(2)
		}
		got := base85n.Encode(data)
		if got != exp.Text() {
			if mismatches < 5 {
				fmt.Fprintf(os.Stderr, "line %d: got  %s\n              want %s\n",
					line, got, exp.Text())
			}
			mismatches++
		}
		// The round trip too, so a case cannot pass by agreeing on garbage.
		back, err := base85n.Decode(got)
		if err != nil || !bytes.Equal(back, data) {
			fmt.Fprintf(os.Stderr, "line %d: round trip failed: %v\n", line, err)
			mismatches++
		}
	}
	fmt.Printf("checked %d cases, %d mismatches\n", line, mismatches)
	if mismatches > 0 {
		os.Exit(1)
	}
}
