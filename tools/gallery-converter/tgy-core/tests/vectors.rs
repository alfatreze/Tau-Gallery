//! The frozen conformance vectors of the symbol layer (docs/conformance/tgy_v0_vectors.json): the Rust encoder must give the stream bytes, the decoder the blocks.
use tgy_core::v0::{self, Grid};

fn b64(s: &str) -> Vec<u8> {
    let val = |c: u8| match c { b'A'..=b'Z' => c - b'A', b'a'..=b'z' => c - b'a' + 26, b'0'..=b'9' => c - b'0' + 52, b'+' => 62, b'/' => 63, _ => 255 };
    let mut out = Vec::new(); let mut acc = 0u32; let mut n = 0;
    for c in s.bytes().filter(|&c| c != b'=') { acc = (acc << 6) | val(c) as u32; n += 6; if n >= 8 { n -= 8; out.push((acc >> n) as u8); acc &= (1 << n) - 1; } }
    out
}
fn hex(s: &str) -> Vec<u8> { (0..s.len() / 2).map(|i| u8::from_str_radix(&s[2 * i..2 * i + 2], 16).unwrap()).collect() }

fn grids(shapes: &[(usize, usize)], raw: &[u8]) -> Vec<Grid> {
    let mut vals = raw.chunks(2).map(|b| i16::from_le_bytes([b[0], b[1]]));
    shapes.iter().map(|&(r, c)| { let mut g = Grid::zeros(r, c); for b in g.blocks.iter_mut() { for v in b.iter_mut() { *v = vals.next().unwrap(); } } g }).collect()
}

#[test]
fn every_conformance_vector_encodes_and_decodes() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../../../docs/conformance/tgy_v0_vectors.json");
    let doc: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(path).unwrap()).unwrap();
    let vs = doc["vectors"].as_array().unwrap();
    assert!(vs.len() >= 9);
    for v in vs {
        let name = v["name"].as_str().unwrap();
        let shapes: Vec<(usize, usize)> = v["shapes"].as_array().unwrap().iter().map(|s| (s[0].as_u64().unwrap() as usize, s[1].as_u64().unwrap() as usize)).collect();
        let g = grids(&shapes, &b64(v["blocks"].as_str().unwrap()));
        let want = hex(v["stream"].as_str().unwrap());
        assert_eq!(v0::encode_tile(&g), want, "{name}: stream differs");
        assert_eq!(v0::decode_tile(&want, &shapes), g, "{name}: decoded blocks differ");
    }
}
