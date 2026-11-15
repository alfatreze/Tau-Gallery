fn main() {
    let d = std::fs::read(std::env::args().nth(1).unwrap()).unwrap();
    let ex = exif::Reader::new().read_from_container(&mut std::io::Cursor::new(&d)).unwrap();
    for f in ex.fields() { println!("{} {:?}", f.tag, f.value); }
}
