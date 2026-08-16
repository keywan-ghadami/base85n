// Scratch harness: corpus expansion ratio of the current build, with a
// round-trip check on every file.
fn main() {
    let mut tb = 0usize;
    let mut tc = 0usize;
    let mut paths: Vec<_> = std::fs::read_dir("../bench/corpus")
        .unwrap()
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.is_file())
        .collect();
    paths.sort();
    for p in paths {
        let d = std::fs::read(&p).unwrap();
        let e = base85n::encode(&d);
        let back = base85n::decode(&e).unwrap();
        assert_eq!(back, d, "roundtrip {:?}", p);
        tb += d.len();
        tc += e.len();
        println!(
            "{:<28} {:.5}",
            p.file_name().unwrap().to_string_lossy(),
            e.len() as f64 / d.len() as f64
        );
    }
    println!("{:<28} {:.5}", "TOTAL", tc as f64 / tb as f64);
}
