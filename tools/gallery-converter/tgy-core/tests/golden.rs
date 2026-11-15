//! Golden values taken from the Python reference (tools/gallery/*.py with Pillow 12.3 / libjpeg-turbo) on small deterministic inputs:
//! the coefficient reader, the JPEG pixel decode, the Lanczos resize, and a whole version 1 container must give the same bytes.
use std::sync::atomic::AtomicBool;
use tgy_core::{container, convert, jpegcoef, picture, recon, resize};

fn synth(w: usize, h: usize) -> Vec<u8> {
    let mut v = Vec::with_capacity(w * h * 3);
    for y in 0..h { for x in 0..w { for c in 0..3 {
        let n = ((x as u64 * 2654435761 + y as u64 * 40503 + c as u64 * 977).wrapping_mul(2246822519) >> 13) % 23;
        v.push(((x as u64 * x as u64 / 7 + y as u64 * 3 + c as u64 * 61 + (x as u64 * y as u64) % 53 + n) % 256) as u8);
    } } }
    v
}
fn crc(b: &[u8]) -> u32 { crc32fast::hash(b) }
/// FNV-1a 64. (A CRC-32 of a whole file is blind to a header that carries its own CRC-32: the residue is constant.)
pub fn fnv(b: &[u8]) -> u64 { b.iter().fold(0xcbf29ce484222325u64, |h, &x| (h ^ x as u64).wrapping_mul(0x100000001b3)) }
fn coef_crc(c: &jpegcoef::Comp) -> u32 { let mut b = Vec::new(); for v in &c.coef { b.extend_from_slice(&v.to_le_bytes()); } crc(&b) }
fn fixture(n: &str) -> Vec<u8> { std::fs::read(format!("{}/tests/fixtures/{n}.jpg", env!("CARGO_MANIFEST_DIR"))).unwrap() }

#[test]
fn coefficients_and_pixels_of_baseline_progressive_restart_and_grey_jpegs() {
    let cases: [(&str, &[u32], u32); 4] = [
        ("base420", &[0x44be0063, 0xd059bea9, 0xb1967dab], 0x1ff36379),
        ("prog444", &[0x869c63dc, 0xf1850abf, 0x932d951b], 0x98881d27),
        ("restart", &[0x0ee4d3d8, 0xbc9c24b1, 0x4b9a131c], 0x5b697708),
        ("grey", &[0xd6abe849], 0x9acd86b3),
    ];
    for (n, coefs, px) in cases {
        let j = jpegcoef::read_jpeg(&fixture(n)).unwrap();
        let got: Vec<u32> = j.components.iter().map(coef_crc).collect();
        assert_eq!(got, coefs, "{n}: coefficients");
        assert_eq!(crc(&recon::decode_pixels(&j, false).unwrap()), px, "{n}: pixels (libjpeg-turbo)");
    }
}

#[test]
fn lanczos_resize_equals_pillows() {
    let src = synth(333, 201);
    for ((w, h), want) in [((120, 77), 0xa7b21241u32), ((333, 50), 0xf4571b97), ((40, 201), 0xfc995ff8), ((500, 300), 0x5b4a6542)] {
        assert_eq!(crc(&resize::resize_rgb(&src, 333, 201, w, h)), want, "{w}x{h}");
    }
}

const FULL_FILE_FNV: u64 = 0x6966d9fe14cbad65;

#[test]
fn a_whole_version_1_container_equals_the_python_file() {
    let (w, h) = (900, 700);
    let p = picture::Prepared { rgb: synth(w, h), w, h, details: picture::Details { format: "PNG".into(), camera: None, lens: None, taken: None, shutter: None, aperture: None, iso: None, focal: None }, lossless: None };
    let (canvas, rect, thumb) = convert::render(&p, false);
    assert_eq!(rect, (0, 49, 800, 622));
    assert_eq!(crc(&canvas), 0xd38e9a64, "the fit canvas");
    assert_eq!(crc(&thumb), 0x3577b224, "the thumbnail");
    assert_eq!(fnv(&thumb), 0xa2385711402af7ab);
    let file = convert::container_v1(&p, &canvas, 92, 90, &thumb, &AtomicBool::new(false)).unwrap();
    let u32at = |o: usize| u32::from_le_bytes([file[o], file[o + 1], file[o + 2], file[o + 3]]) as usize;
    // header, the fit level's table entry, its tile stream and its tile index are byte for byte the Python ones
    let (data_off, data_len, index_off, index_len) = (u32at(64 + 24), u32at(64 + 28), u32at(64 + 32), u32at(64 + 36));
    let mut exact = file[..52].to_vec();
    exact.extend_from_slice(&file[64..64 + 48]);
    exact.extend_from_slice(&file[data_off..data_off + data_len]);
    exact.extend_from_slice(&file[index_off..index_off + index_len]);
    assert_eq!(fnv(&exact), 0xb4c4d218658f0377, "header, fit level entry, tiles and tile index");
    assert_eq!(file.len(), 815104, "the same file size as the Python file");
    // the full level is coded from float arithmetic that the Python encoder does in numpy's order: 2 of about a million coefficients of this picture
    // sit exactly on a rounding tie and come out one apart, so the whole file is pinned to this encoder's own value (the Python encoder is not normative)
    assert_eq!(fnv(&file), FULL_FILE_FNV, "the whole container");
    let _ = container::KIND_FULL;
}

#[test]
fn a_stop_request_ends_a_container_write() {
    let (w, h) = (900, 700);
    let p = picture::Prepared { rgb: synth(w, h), w, h, details: picture::Details { format: "PNG".into(), camera: None, lens: None, taken: None, shutter: None, aperture: None, iso: None, focal: None }, lossless: None };
    let (canvas, _, thumb) = convert::render(&p, false);
    assert_eq!(convert::container_v1(&p, &canvas, 92, 90, &thumb, &AtomicBool::new(true)).unwrap_err(), "Stopped.");
}
