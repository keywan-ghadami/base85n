//! Instruction-count harness, the Rust twin of the C one: encodes or decodes one
//! fixed input once, so `callgrind` can be run over it and the setup subtracted
//! with a `none` run. Not part of the library.

use std::hint::black_box;

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let kind = &args[1];
    let phase = &args[2];
    let n: usize = args[3].parse().unwrap();

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
            let src = std::fs::read(&args[4]).unwrap();
            (0..n).map(|i| src[i % src.len()]).collect()
        }
        _ => {
            let lit = b"hello world this is text 0123456789 ";
            let mut v = Vec::with_capacity(n);
            while v.len() < n {
                for _ in 0..40 {
                    if v.len() == n { break; }
                    v.push(xorshift());
                }
                for &c in lit {
                    if v.len() == n { break; }
                    v.push(c);
                }
            }
            v
        }
    };

    let enc = base85n::encode(&data);

    match phase.as_str() {
        "encode" => println!("{}", black_box(base85n::encode(black_box(&data))).len()),
        "decode" => println!("{}", black_box(base85n::decode(black_box(&enc))).unwrap().len()),
        _ => println!("{}", enc.len()),
    }
}
