// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! What the parallel encoder buys, on this machine, for a given input.
//!
//! Usage: `cargo run --release --example parallel [file] [repeats]`
//!
//! With no file it uses a synthetic mixture. The file is concatenated with
//! itself until it is at least 16 MiB, since anything smaller is one chunk.
//! Every result is checked against the sequential encoder before it is timed:
//! a number for output that differs would not mean anything.

use std::time::Instant;

fn synthetic(len: usize) -> Vec<u8> {
    let mut state = 0x2545_F491_4F6C_DD1Du64;
    let mut next = move || {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        (state >> 32) as u32
    };
    let mut out = Vec::with_capacity(len);
    while out.len() < len {
        match next() % 4 {
            0 => out.extend(std::iter::repeat_n(0u8, 1 + (next() % 40) as usize)),
            1 => out.extend_from_slice(b"    \"key\": [1, 2, 3],\n"),
            2 => out.extend_from_slice(b"the quick brown fox jumps over the lazy dog\n"),
            _ => {
                for _ in 0..1 + next() % 24 {
                    out.push((next() >> 5) as u8);
                }
            }
        }
    }
    out.truncate(len);
    out
}

fn main() {
    let mut args = std::env::args().skip(1);
    let path = args.next();
    let repeats: usize = args.next().and_then(|s| s.parse().ok()).unwrap_or(3);

    let mut data = match &path {
        Some(p) => std::fs::read(p).expect("read input"),
        None => synthetic(16 << 20),
    };
    if data.is_empty() {
        eprintln!("empty input");
        return;
    }
    let seed = data.clone();
    while data.len() < 16 << 20 {
        data.extend_from_slice(&seed);
    }

    let best = |f: &dyn Fn() -> String| -> (f64, String) {
        let mut best = f64::MAX;
        let mut out = String::new();
        for _ in 0..repeats {
            let t = Instant::now();
            let s = f();
            best = best.min(t.elapsed().as_secs_f64());
            out = s;
        }
        (best, out)
    };

    let (t1, seq) = best(&|| base85n::encode(&data));
    println!(
        "{} MiB of {}",
        data.len() >> 20,
        path.as_deref().unwrap_or("synthetic mixed input")
    );
    println!("{:>8}  {:>9}  {:>8}  output", "threads", "MB/s", "speedup");
    println!("{:>8}  {:>9.1}  {:>8}  {}", 1, data.len() as f64 / t1 / 1e6, "-", seq.len());
    for threads in [2usize, 4, 8, 16] {
        let (t, par) = best(&|| base85n::encode_parallel(&data, threads));
        println!(
            "{:>8}  {:>9.1}  {:>7.2}x  {}",
            threads,
            data.len() as f64 / t / 1e6,
            t1 / t,
            if par == seq { "identical".to_string() } else { format!("DIFFERS ({})", par.len()) }
        );
    }
}
