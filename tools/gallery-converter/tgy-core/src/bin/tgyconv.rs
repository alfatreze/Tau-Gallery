use tgy_core::{jpegcoef, recon, resize};
fn main() {
    let a: Vec<String> = std::env::args().collect();
    let cmd = a.get(1).map(|s| s.as_str()).unwrap_or("");
    if cmd != "pixels" && cmd != "resize" && cmd != "coefs" && cmd != "enc" && cmd != "prep" { std::process::exit(convert_cli(&a[1..])); }
    if cmd == "prep" {
        let p = tgy_core::picture::prepare(&std::fs::read(&a[2]).expect("read")).expect("prepare");
        std::fs::write(&a[3], &p.rgb).unwrap();
        println!("{}x{}", p.w, p.h);
        return;
    }
    if cmd == "enc" {
        let p = tgy_core::picture::prepare(&std::fs::read(&a[2]).expect("read")).expect("prepare");
        let j = tgy_core::encode::encode_rgb(&p.rgb, p.w, p.h, a[3].parse().unwrap());
        let mut out = Vec::new();
        for c in &j.components { for v in &c.coef { out.extend_from_slice(&v.to_le_bytes()); } }
        std::fs::write(&a[4], out).unwrap();
        return;
    }
    if cmd == "pixels" || cmd == "resize" {
        let data = std::fs::read(&a[2]).expect("read");
        let j = jpegcoef::read_jpeg(&data).expect("jpeg");
        let t = std::time::Instant::now();
        let mut rgb = recon::decode_pixels(&j, false).expect("pixels");
        eprintln!("decode {:?}", t.elapsed());
        let (mut w, mut h) = (j.width, j.height);
        if cmd == "resize" {
            let (dw, dh): (usize, usize) = (a[3].parse().unwrap(), a[4].parse().unwrap());
            let t = std::time::Instant::now();
            rgb = resize::resize_rgb(&rgb, w, h, dw, dh); w = dw; h = dh;
            eprintln!("resize {:?}", t.elapsed());
        }
        println!("{}x{} {:08x}", w, h, crc32fast::hash(&rgb));
        return;
    }
    if cmd == "coefs" {
        let data = std::fs::read(&a[2]).expect("read");
        match jpegcoef::read_jpeg(&data) {
            Ok(j) => {
                println!("{}x{} prog={} ri={}", j.width, j.height, j.progressive, j.restart_interval);
                for c in &j.components {
                    let mut bytes = Vec::with_capacity(c.coef.len() * 2);
                    for v in &c.coef { bytes.extend_from_slice(&v.to_le_bytes()); }
                    println!("{} {}x{} {}x{} {:08x}", c.id, c.h, c.v, c.blocks_w, c.blocks_h, crc32fast::hash(&bytes));
                }
            }
            Err(e) => { println!("ERR {e}"); std::process::exit(1); }
        }
    }
}


fn convert_cli(args: &[String]) -> i32 {
    use tgy_core::convert::{run, Options};
    let mut pos: Vec<&String> = Vec::new();
    let mut o = Options::new(Default::default(), Default::default());
    let mut i = 0;
    while i < args.len() {
        let k = args[i].as_str();
        let mut val = || { i += 1; args.get(i).cloned().unwrap_or_default() };
        match k {
            "--card-dir" => o.card_dir = val(),
            "--index-name" => o.index_name = val(),
            "--upscale" => o.upscale = true,
            "--levels" => o.levels = true,
            "--format" => o.format_v1 = val() == "v1",
            "--quality" => o.quality = val().parse().unwrap_or(92),
            "--fit-quality" => o.fit_quality = val().parse().unwrap_or(90),
            "--card-layout" => o.card_layout = true,
            "--merge" => o.merge = true,
            "--index-format" => o.index_v1 = val() != "v0",
            "--sort" => o.sort = val(),
            "--jobs" => { let n: usize = val().parse().unwrap_or(0); if n > 0 { rayon::ThreadPoolBuilder::new().num_threads(n).build_global().ok(); } }
            _ => pos.push(&args[i]),
        }
        i += 1;
    }
    if pos.len() != 2 { eprintln!("usage: tgyconv SRC OUT [options]"); return 2; }
    o.src = pos[0].into();
    o.out = pos[1].into();
    let cancel = std::sync::atomic::AtomicBool::new(false);
    run(&o, &cancel, &mut |v| println!("{v}"))
}
