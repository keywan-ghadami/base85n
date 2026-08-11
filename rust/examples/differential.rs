//! Differential harness: checks this crate's encoder against reference
//! encodings produced elsewhere (the Python implementation generates them), and
//! exercises the decoder's round trip and its inter-token-whitespace retry.
//!
//! Both files are line-oriented and parallel: `inputs` holds one hex-encoded
//! byte string per line, `expected` the Base85N encoding of the same line.
//!
//!     cargo run --release --example differential -- inputs.txt expected.txt

use std::fs;

fn from_hex(s: &str) -> Vec<u8> {
    (0..s.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&s[i..i + 2], 16).expect("hex digit pair"))
        .collect()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 3 {
        eprintln!("usage: differential <inputs.txt> <expected.txt>");
        std::process::exit(2);
    }
    let inputs = fs::read_to_string(&args[1]).expect("inputs file");
    let expected = fs::read_to_string(&args[2]).expect("expected file");

    let mut bad = 0usize;
    let mut n = 0usize;
    for (line, (hex, want)) in inputs.lines().zip(expected.lines()).enumerate() {
        n += 1;
        let data = from_hex(hex);
        let got = base85n::encode(&data);
        if got != want {
            bad += 1;
            if bad < 4 {
                println!(
                    "line {} ENCODE mismatch\n  want {}\n  got  {}",
                    line + 1,
                    &want[..want.len().min(80)],
                    &got[..got.len().min(80)]
                );
            }
            continue;
        }
        match base85n::decode(&got) {
            Ok(back) if back == data => {}
            other => {
                bad += 1;
                if bad < 4 {
                    println!("line {} DECODE mismatch: {:?}", line + 1, other.err());
                }
                continue;
            }
        }
        // Whitespace is ignorable between constructs, and the decoder only
        // notices it after a failed scan, so every position is worth trying.
        for p in [0usize, got.len() / 2, got.len()] {
            let ws = format!("{} \n\t\r{}", &got[..p], &got[p..]);
            match base85n::decode(&ws) {
                Ok(back) if back == data => {}
                other => {
                    bad += 1;
                    if bad < 6 {
                        println!("line {} WS-RETRY mismatch at {}: {:?}", line + 1, p, other.err());
                    }
                    break;
                }
            }
        }
    }
    println!("checked {n} cases, {bad} mismatches");
    if bad > 0 {
        std::process::exit(1);
    }
}
