//! The library index (TGIX v0 and v1), the details, the orientation transforms and the date arithmetic against values taken from the Python reference.
use tgy_core::{convert, index, picture};

fn fnv(b: &[u8]) -> u64 { b.iter().fold(0xcbf29ce484222325u64, |h, &x| (h ^ x as u64).wrapping_mul(0x100000001b3)) }
fn crc(b: &[u8]) -> u32 { crc32fast::hash(b) }
fn th(n: usize, sz: usize) -> Vec<u8> { (0..sz).map(|i| ((i * 7 + n) % 256) as u8).collect() }
fn none() -> picture::Details { picture::Details { format: String::new(), camera: None, lens: None, taken: None, shutter: None, aperture: None, iso: None, focal: None } }

fn entries() -> Vec<index::Entry1> {
    let full = picture::Details { format: "JPEG".into(), camera: Some("Canon EOS R5".into()), lens: Some("RF35".into()), taken: Some((2024, 5, 6, 7, 8, 9)), shutter: Some((1, 250)), aperture: Some(28), iso: Some(400), focal: Some(350) };
    let png = picture::Details { format: "PNG".into(), ..none() };
    let mk = |i: usize, name: &str, size, w, h, folder, taken, d: Vec<u8>| index::Entry1 { path: format!("Assets/tau_gallery/common/photos/{}.tgy", ["a", "b", "c"][i]), name: name.into(), size, width: w, height: h, folder, taken, details: d, thumb: th(i, index::THUMB1_BYTES) };
    vec![mk(0, "Alpha", 1000, 800, 600, 0, convert::taken_seconds(full.taken), index::pack_details(&full)), mk(1, "B\u{e9}ta", 2000, 640, 480, 1, 0, vec![]), mk(2, "Gamma", 3000, 100, 100, 1, 5, index::pack_details(&png))]
}

#[test]
fn the_version_1_index_is_the_python_one_for_each_sort_mode() {
    let folders = [index::Folder { name: "Alpine".into() }, index::Folder { name: "S\u{e9}a".into() }];
    for (sort, want) in [(index::SORT_NAME, 0xad8382842feb204du64), (index::SORT_TAKEN, 0x84f7b89fa5227c49), (index::SORT_CONVERTED, 0xb3cd50f2da85845c)] {
        assert_eq!(fnv(&index::pack_index_v1(&entries(), &folders, sort).unwrap()), want, "sort {sort}");
    }
}

#[test]
fn the_version_0_index_is_the_python_one() {
    let e: Vec<index::Entry0> = entries().into_iter().enumerate().map(|(i, e)| index::Entry0 { path: e.path, name: e.name, size: e.size, width: e.width, height: e.height, thumb: th(i, index::THUMB_BYTES) }).collect();
    assert_eq!(fnv(&index::pack_index_v0(&e).unwrap()), 0x0dbcf932e4412e37);
}

#[test]
fn an_index_written_is_read_back_whole() {
    let folders = [index::Folder { name: "Alpine".into() }, index::Folder { name: "S\u{e9}a".into() }];
    let d = index::pack_index_v1(&entries(), &folders, index::SORT_TAKEN).unwrap();
    let (ver, old) = index::parse_index(&d).unwrap();
    assert_eq!((ver, old.len()), (1, 3));
    assert_eq!((old[1].folder_name.as_str(), old[1].name.as_str(), old[2].taken, old[0].details.len()), ("S\u{e9}a", "B\u{e9}ta", 5, entries()[0].details.len()));
    assert_eq!(old[0].thumb, th(0, index::THUMB1_BYTES));
    let mut bad = d.clone(); bad[34] ^= 1;
    assert!(index::parse_index(&bad).is_err(), "the header checksum catches a changed byte");
}

#[test]
fn exif_details_orientation_and_the_dates_equal_the_python_reference() {
    let d = std::fs::read(format!("{}/tests/fixtures/exif.jpg", env!("CARGO_MANIFEST_DIR"))).unwrap();
    let p = picture::prepare(&d).unwrap();
    assert_eq!((p.w, p.h), (30, 40), "orientation 6 turns the 40 x 30 picture upright");
    assert!(p.lossless.is_none(), "a rotated picture cannot keep its own coefficients");
    let hex: String = index::pack_details(&p.details).iter().map(|b| format!("{b:02x}")).collect();
    assert_eq!(hex, "01044a504547021b4e494b4f4e20434f52504f524154494f4e204e494b4f4e205a2037030d4e494b4b4f52205a2038356d6d0407e7070c1f173b3a050801000000401f000006021c0007046400000008025203");
    let src: Vec<u8> = (0..7 * 5 * 3).map(|i| (i * 37 % 256) as u8).collect();
    for (o, want, dims) in [(2u8, 0x73a12f6du32, (7, 5)), (3, 0xd888b429, (7, 5)), (4, 0x53cb0449, (7, 5)), (5, 0x4f0a1a3b, (5, 7)), (6, 0x613ea199, (5, 7)), (7, 0x60463da7, (5, 7)), (8, 0x6024acbf, (5, 7))] {
        let (out, w, h) = picture::transpose(src.clone(), 7, 5, o);
        assert_eq!((crc(&out), (w, h)), (want, dims), "orientation {o}");
    }
    let t = |x| convert::taken_seconds(Some(x));
    assert_eq!([t((2024, 5, 6, 7, 8, 9)), t((2000, 1, 1, 0, 0, 0)), t((2000, 1, 1, 0, 0, 1)), t((2099, 12, 31, 23, 59, 59)), t((2024, 2, 30, 1, 1, 1)), t((1999, 12, 31, 23, 59, 59)), t((2024, 2, 29, 12, 0, 0))], [768294489, 0, 1, 3155759999u32, 0, 0, 762523200]);
}
