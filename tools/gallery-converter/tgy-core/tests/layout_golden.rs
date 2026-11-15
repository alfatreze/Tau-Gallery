//! More values from the Python reference: the quality scaling of the quantisation tables, the placement rectangles, more Lanczos ratios, and the details of a second EXIF fixture.
use tgy_core::{convert, encode, index, picture, resize};

fn fnv(b: &[u8]) -> u64 { b.iter().fold(0xcbf29ce484222325u64, |h, &x| (h ^ x as u64).wrapping_mul(0x100000001b3)) }
fn crc(b: &[u8]) -> u32 { crc32fast::hash(b) }

#[test]
fn quantisation_tables_scale_with_the_quality_like_libjpegs() {
    let cases: [(u32, u64, u64); 11] = [(1, 0x84cc4da0e20ecde5, 0x84cc4da0e20ecde5), (10, 0x3b0809463886b163, 0xece478bac479aa79), (25, 0x685ea11212c91a4d, 0xa466c182b61d2f0b), (49, 0x5342890fc78d1778, 0x02154626d2c331bd),
        (50, 0x6debea4dab5fa5bd, 0xeb5203fcdf724f2a), (51, 0x056af8d5164e936a, 0x836fdc9685c86ce3), (75, 0xa1915355df18823b, 0x4ae7a21fa671c9dd), (85, 0x7d57781b86f6a682, 0xe136fe05501f347b),
        (90, 0xc7fadd97dd01b2a9, 0xa1aee260ff8e56c9), (92, 0x42d9f46ca57bb88f, 0x2c6e38016e8ae242), (100, 0xea7805377dec9065, 0xea7805377dec9065)];
    for (q, luma, chroma) in cases {
        let b = |t: &[u16; 64]| t.iter().map(|&v| v as u8).collect::<Vec<u8>>();
        assert_eq!(fnv(&b(&encode::scaled_table(&encode::STD_LUMA, q))), luma, "luma at {q}");
        assert_eq!(fnv(&b(&encode::scaled_table(&encode::STD_CHROMA, q))), chroma, "chroma at {q}");
    }
}

#[test]
fn the_picture_rectangle_is_the_pythons_for_odd_and_even_sizes() {
    let cases = [((900, 700, 800, 720, false), (0, 49, 800, 622)), ((333, 201, 96, 72, false), (0, 7, 96, 58)), ((700, 900, 800, 720, false), (120, 0, 560, 720)), ((701, 899, 800, 720, false), (118, 0, 561, 720)),
        ((1000, 1000, 800, 720, false), (40, 0, 720, 720)), ((101, 77, 800, 720, false), (348, 321, 101, 77)), ((101, 77, 800, 720, true), (0, 55, 800, 610)), ((5, 3, 96, 96, false), (44, 46, 5, 3)), ((4000, 100, 800, 720, false), (0, 350, 800, 20))];
    for ((w, h, cw, ch, up), want) in cases { assert_eq!(convert::fit_rect(w, h, cw, ch, up), want, "{w}x{h} in {cw}x{ch} upscale {up}"); }
}

#[test]
fn lanczos_at_exact_ratios_and_in_one_direction_only() {
    let src: Vec<u8> = (0..333 * 201 * 3).map(|i| (i * 31 % 256) as u8).collect();
    for ((w, h), want) in [((111, 67), 0x15db8422u32), ((166, 100), 0xecd84aa7), ((333, 67), 0x96fd3608), ((222, 201), 0x3f61f0d2), ((666, 402), 0xe45d4934), ((1000, 50), 0x601114e2), ((332, 200), 0x1f813aad), ((10, 10), 0xc6c736e1)] {
        assert_eq!(crc(&resize::resize_rgb(&src, 333, 201, w, h)), want, "{w}x{h}");
    }
}

#[test]
fn a_camera_named_like_its_maker_and_the_rounding_of_aperture_and_focal_length() {
    let d = std::fs::read(format!("{}/tests/fixtures/exif2.jpg", env!("CARGO_MANIFEST_DIR"))).unwrap();
    let p = picture::prepare(&d).unwrap();
    assert_eq!(p.details.camera.as_deref(), Some("Canon EOS R5"), "the model already starts with the make");
    assert_eq!((p.details.aperture, p.details.focal, p.details.iso, p.details.shutter), (Some(18), Some(360), Some(6400), Some((1, 3))), "17.9 and 359.6 round, they are not cut");
    let hex: String = index::pack_details(&p.details).iter().map(|b| format!("{b:02x}")).collect();
    assert_eq!(hex, "01044a504547020c43616e6f6e20454f53205235050801000000030000000602120007040019000008026801");
}
