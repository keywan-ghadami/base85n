// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Wall-clock harness, the Rust twin of `bench/throughput/time.c`: same
//! generated inputs, same loop, same reporting, so that the two numbers can be
//! divided by one another. `bench/throughput/run.sh` drives both.
//!
//! Usage: `cargo run --release --example throughput -- <kind> <phase> <bytes> <reps> [file]`

use std::hint::black_box;
use std::time::Instant;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 5 {
        eprintln!("usage: throughput <random|text|mixed> <encode|decode> <bytes> <reps> [file]");
        std::process::exit(2);
    }
    let kind = &args[1];
    let phase = &args[2];
    let n: usize = args[3].parse().expect("a byte count");
    let reps: u32 = args[4].parse().expect("a repetition count");

    let mut state: u64 = 0x2545F4914F6CDD1D;
    let mut xorshift = move || {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        (state >> 24) as u8
    };

    let data: Vec<u8> = match kind.as_str() {
        "random" => (0..n).map(|_| xorshift()).collect(),
        "text" => {
            let src = std::fs::read(&args[5]).expect("the file named on the command line");
            // Tile the file until the buffer is full, as the C harness does.
            (0..n).map(|i| src[i % src.len()]).collect()
        }
        _ => {
            let lit = b"hello world this is text 0123456789 ";
            let mut v = Vec::with_capacity(n);
            while v.len() < n {
                for _ in 0..40 {
                    if v.len() == n {
                        break;
                    }
                    v.push(xorshift());
                }
                for &c in lit {
                    if v.len() == n {
                        break;
                    }
                    v.push(c);
                }
            }
            v
        }
    };

    let enc = base85n::encode(&data);
    let mut best = f64::MAX;
    let mut sink = 0usize;
    for _ in 0..reps {
        let t0 = Instant::now();
        match phase.as_str() {
            "encode" => sink += black_box(base85n::encode(black_box(&data))).len(),
            _ => sink += black_box(base85n::decode(black_box(&enc))).expect("valid input").len(),
        }
        let dt = t0.elapsed().as_secs_f64();
        if dt < best {
            best = dt;
        }
    }

    eprintln!("encoded {} bytes (checksum of runs {sink})", enc.len());
    println!("{:.2}", n as f64 / best / (1024.0 * 1024.0));
}
