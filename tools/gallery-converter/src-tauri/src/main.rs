// Tau Gallery Converter: picks a picture or a folder and converts it with the converter built into the app (the `tgy-core` crate, a Rust port of tools/gallery/tgy_convert.py:
// no Python, no Pillow, no numpy). The converter writes the card's own tree (Assets/tau_gallery/common/...) into the chosen folder; the app relays its one-JSON-event-a-line
// records to the page and can stop it between pictures and between tiles.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tauri::{AppHandle, Emitter, Manager, State};
use tgy_core::convert::{self, Options};

const EXT: [&str; 8] = ["jpg", "jpeg", "png", "bmp", "gif", "tif", "tiff", "webp"];

/// The running conversion's stop flag, while one runs.
#[derive(Default)]
struct Job(Mutex<Option<Arc<AtomicBool>>>);

#[derive(Serialize)]
struct Scan { kind: String, name: String, pictures: usize, folders: usize }

#[derive(Serialize, Debug)]
struct Summary { pictures: u64, converted: u64, skipped: u64, folders: u64, out: String }

fn is_picture(p: &Path) -> bool {
    p.extension().and_then(|e| e.to_str()).map(|e| EXT.contains(&e.to_ascii_lowercase().as_str())).unwrap_or(false)
}

#[tauri::command]
fn scan(path: String) -> Scan {
    let p = Path::new(&path);
    let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| path.clone());
    if p.is_file() { return Scan { kind: if is_picture(p) { "file".into() } else { "unsupported".into() }, name, pictures: usize::from(is_picture(p)), folders: 0 }; }
    if p.is_dir() {
        let count = |d: &Path| std::fs::read_dir(d).map(|rd| rd.filter_map(|e| e.ok()).filter(|e| e.path().is_file() && is_picture(&e.path())).count()).unwrap_or(0);
        let top = count(p);
        // one level of folders: a subfolder that holds pictures is a folder of the library (hidden ones are not looked at)
        let subs: Vec<usize> = std::fs::read_dir(p).map(|rd| rd.filter_map(|e| e.ok()).filter(|e| e.path().is_dir() && !e.file_name().to_string_lossy().starts_with('.')).map(|e| count(&e.path())).filter(|n| *n > 0).collect()).unwrap_or_default();
        return Scan { kind: "folder".into(), name, pictures: top + subs.iter().sum::<usize>(), folders: subs.len() };
    }
    Scan { kind: "missing".into(), name, pictures: 0, folders: 0 }
}

#[tauri::command]
fn default_output() -> String {
    let home = std::env::var("HOME").or_else(|_| std::env::var("USERPROFILE")).unwrap_or_default();
    Path::new(&home).join("Documents").join("Tau Gallery").to_string_lossy().into_owned()
}

/// The converter writes the card's own tree (Assets/tau_gallery/common/...) under the output folder, so an output that is already inside that tree
/// (the user picked .../Assets/tau_gallery/common) means its root: the tree is never written inside itself.
fn normalize_output(path: &str) -> String {
    let p = path.trim_end_matches(['/', '\\']);
    let parts: Vec<&str> = p.split(['/', '\\']).collect();
    for i in 0..parts.len().saturating_sub(1) {
        if parts[i] == "Assets" && parts[i + 1] == "tau_gallery" {
            let sep = if p.contains('\\') { "\\" } else { "/" };           // a Windows path keeps its backslashes
            let root = parts[..i].join(sep);
            return if root.is_empty() { sep.to_string() } else if root.ends_with(':') { format!("{root}{sep}") } else { root };    // "E:" alone would mean the current folder of drive E
        }
    }
    p.to_string()
}

#[derive(Serialize, Debug)]
struct Volume { name: String, path: String, free_gb: f64 }

#[cfg(not(windows))]
/// The disks mounted under /Volumes (or /media, /run/media) other than the system disk, with their free space.
fn volumes_in(roots: &[&str]) -> Vec<Volume> {
    let mut out = Vec::new();
    for root in roots {
        let Ok(rd) = std::fs::read_dir(root) else { continue };
        let mut names: Vec<PathBuf> = rd.filter_map(|e| e.ok()).map(|e| e.path()).collect();
        names.sort();
        for p in names {
            if !p.is_dir() { continue }
            if std::fs::canonicalize(&p).map(|c| c == Path::new("/")).unwrap_or(false) { continue }          // the system disk is listed as a link to /
            let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
            if name.starts_with('.') { continue }
            out.push(Volume { name, free_gb: free_gb(&p), path: p.to_string_lossy().into_owned() });
        }
    }
    out
}
#[cfg(not(windows))]
fn free_gb(p: &Path) -> f64 {
    let Ok(o) = Command::new("df").args(["-k"]).arg(p).output() else { return -1.0 };
    String::from_utf8_lossy(&o.stdout).lines().nth(1).and_then(|l| l.split_whitespace().nth(3)).and_then(|s| s.parse::<f64>().ok()).map(|k| (k / 1048576.0 * 10.0).round() / 10.0).unwrap_or(-1.0)
}
/// Windows: the drives D: to Z: that exist (the system drive is left out); the label and the free space are not read.
#[cfg(windows)]
fn windows_drives(system: &str) -> Vec<Volume> {
    ('D'..='Z').filter(|l| !system.to_ascii_uppercase().starts_with(*l)).map(|l| (l, format!("{l}:\\"))).filter(|(_, p)| Path::new(p).is_dir()).map(|(l, p)| Volume { name: format!("Drive {l}:"), path: p, free_gb: -1.0 }).collect()
}
#[tauri::command]
#[cfg(windows)]
fn list_volumes() -> Vec<Volume> { windows_drives(&std::env::var("SystemDrive").unwrap_or_else(|_| "C:".into())) }
#[tauri::command]
#[cfg(not(windows))]
fn list_volumes() -> Vec<Volume> {
    let user = std::env::var("USER").unwrap_or_default();
    let media = format!("/media/{user}"); let run_media = format!("/run/media/{user}");
    volumes_in(&["/Volumes", media.as_str(), run_media.as_str()])
}
#[tauri::command]
fn normalized(path: String) -> String { normalize_output(&path) }

/// Runs one conversion in this process and relays its events. The stop flag sits in `job` while it runs so `cancel` can reach it. Returns the converter's own "done" summary.
fn run_conversion(job: &Mutex<Option<Arc<AtomicBool>>>, opts: &Options, mut on_event: impl FnMut(serde_json::Value)) -> Result<Summary, String> {
    let flag = Arc::new(AtomicBool::new(false));
    {
        let mut slot = job.lock().map_err(|_| "job lock")?;
        if slot.is_some() { return Err("A conversion is already running.".into()); }
        *slot = Some(flag.clone());
    }
    let mut done: Option<Summary> = None;
    let mut last_error = String::new();
    convert::run(opts, &flag, &mut |v| {
        match v.get("event").and_then(|e| e.as_str()) {
            Some("done") => done = Some(Summary { pictures: v["pictures"].as_u64().unwrap_or(0), converted: v["converted"].as_u64().unwrap_or(0), skipped: v["skipped"].as_u64().unwrap_or(0), folders: v["folders"].as_u64().unwrap_or(0), out: v["out"].as_str().unwrap_or("").to_string() }),
            Some("error") => last_error = v["error"].as_str().unwrap_or("").to_string(),
            _ => {}
        }
        on_event(v);
    });
    if let Ok(mut slot) = job.lock() { slot.take(); }
    if let Some(s) = done { return Ok(s); }
    if !last_error.is_empty() { return Err(last_error); }
    Err("The converter stopped without finishing.".into())
}

fn stop_job(job: &Mutex<Option<Arc<AtomicBool>>>) -> bool {
    match job.lock() { Ok(slot) => match slot.as_ref() { Some(f) => { f.store(true, Ordering::Relaxed); true } None => false }, Err(_) => false }
}

/// The converter's options for one run. `format` is "v0" (the picture as raw tiles, every Tau Gallery core; `levels` adds the half and 1:1 zoom stops) or "v1" (the compressed container the hardware TGY decoder draws;
/// its zoom level is written by the converter itself for a picture larger than the screen, so `levels` means nothing there; `quality` is the quality of that level for a picture that is not a JPEG, `fit_quality` the fit level's).
fn convert_options(input: &str, output: &str, levels: bool, upscale: bool, merge: bool, format: &str, quality: u32, fit_quality: u32, sort: &str, old_index: bool) -> Result<Options, String> {
    if format != "v0" && format != "v1" { return Err(format!("Unknown format {format}.")); }
    if sort != "name" && sort != "taken" && sort != "converted" { return Err(format!("Unknown sort order {sort}.")); }
    if !(1..=100).contains(&quality) || !(1..=100).contains(&fit_quality) { return Err("The quality is a number from 1 to 100.".into()); }
    let mut o = Options::new(PathBuf::from(input), PathBuf::from(normalize_output(output)));
    o.card_layout = true;
    o.format_v1 = format == "v1";
    if o.format_v1 { o.quality = quality; o.fit_quality = fit_quality; } else { o.levels = levels; }
    o.upscale = upscale;
    o.merge = merge;
    if old_index { o.index_v1 = false; } else { o.sort = sort.to_string(); }          // the flat index of a gallery before 1.0 (no folders, no details, no sort mode)
    Ok(o)
}

#[tauri::command]
async fn convert(app: AppHandle, input: String, output: String, levels: bool, upscale: bool, merge: bool, format: String, quality: u32, fit_quality: u32, sort: String, old_index: bool) -> Result<Summary, String> {
    let opts = convert_options(&input, &output, levels, upscale, merge, &format, quality, fit_quality, &sort, old_index)?;
    let handle = app.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let job = handle.state::<Job>();
        let h2 = handle.clone();
        run_conversion(&job.0, &opts, move |v| { let _ = h2.emit("convert-event", v); })
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn cancel(state: State<'_, Job>) -> bool { stop_job(&state.0) }

#[tauri::command]
fn reveal(path: String) -> Result<(), String> {
    let p = Path::new(&path);
    if !p.exists() { return Err("That folder does not exist.".into()); }
    #[cfg(target_os = "macos")] let r = Command::new("open").arg(p).spawn();
    #[cfg(target_os = "windows")] let r = Command::new("explorer").arg(p).spawn();
    #[cfg(all(not(target_os = "macos"), not(target_os = "windows")))] let r = Command::new("xdg-open").arg(p).spawn();
    r.map(|_| ()).map_err(|e| e.to_string())
}

fn main() {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .manage(Job::default())
        .invoke_handler(tauri::generate_handler![scan, default_output, list_volumes, normalized, convert, cancel, reveal])
        .run(tauri::generate_context!())
        .expect("error while running Tau Gallery Converter");
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tmp(name: &str) -> PathBuf { let d = std::env::temp_dir().join(format!("tgc-{}-{}", name, std::process::id())); let _ = std::fs::remove_dir_all(&d); std::fs::create_dir_all(&d).unwrap(); d }
    fn make_png(path: &Path, w: u32, h: u32) {
        let mut im = image::RgbImage::new(w, h);
        for (x, y, p) in im.enumerate_pixels_mut() { *p = image::Rgb([(x * 255 / w.max(1)) as u8, (y * 255 / h.max(1)) as u8, 90]); }
        im.save(path).unwrap();
    }
    fn opts(input: &Path, out: &Path, merge: bool) -> Options { convert_options(input.to_str().unwrap(), out.to_str().unwrap(), false, false, merge, "v0", 92, 90, "name", false).unwrap() }

    #[test]
    fn pictures_are_recognised_by_extension() {
        assert!(is_picture(Path::new("a.JPG")) && is_picture(Path::new("b.webp")) && is_picture(Path::new("c.tiff")));
        assert!(!is_picture(Path::new("notes.txt")) && !is_picture(Path::new("noext")) && !is_picture(Path::new(".png.part")));
    }

    #[test]
    fn scan_tells_a_picture_a_folder_and_the_rest_apart() {
        let d = tmp("scan");
        make_png(&d.join("one.png"), 40, 30); make_png(&d.join("two.PNG"), 40, 30); std::fs::write(d.join("notes.txt"), "x").unwrap(); std::fs::create_dir(d.join("sub")).unwrap(); make_png(&d.join("sub").join("deep.png"), 10, 10);
        let f = scan(d.join("one.png").to_string_lossy().into_owned()); assert_eq!((f.kind.as_str(), f.pictures), ("file", 1));
        let t = scan(d.join("notes.txt").to_string_lossy().into_owned()); assert_eq!((t.kind.as_str(), t.pictures), ("unsupported", 0));
        let dir = scan(d.to_string_lossy().into_owned()); assert_eq!((dir.kind.as_str(), dir.pictures, dir.folders), ("folder", 3, 1), "a subfolder with pictures is a folder of the library and its pictures count");
        let flat = scan(d.join("sub").to_string_lossy().into_owned()); assert_eq!((flat.kind.as_str(), flat.pictures, flat.folders), ("folder", 1, 0));
        let m = scan(d.join("nope").to_string_lossy().into_owned()); assert_eq!((m.kind.as_str(), m.pictures), ("missing", 0));
    }

    #[test]
    fn a_folder_converts_into_the_cards_tree_and_a_single_picture_is_added_to_it() {
        let d = tmp("conv"); let src = d.join("in"); std::fs::create_dir(&src).unwrap(); let out = d.join("out");
        make_png(&src.join("a.png"), 900, 600); make_png(&src.join("b.png"), 600, 900); std::fs::write(src.join("notes.txt"), "x").unwrap();
        let job = Mutex::new(None); let mut events: Vec<String> = Vec::new();
        let s = run_conversion(&job, &opts(&src, &out, false), |v| events.push(v["event"].as_str().unwrap_or("").to_string())).unwrap();
        assert_eq!((s.pictures, s.converted, s.skipped), (2, 2, 0));
        assert_eq!(events, ["plan", "converted", "converted", "done"]);
        assert!(out.join("Assets/tau_gallery/common/tau-library.tdb").is_file() && out.join("Assets/tau_gallery/common/photos/a.tgy").is_file());
        let one = d.join("c.png"); make_png(&one, 500, 500);
        let s = run_conversion(&job, &opts(&one, &out, true), |_| {}).unwrap();
        assert_eq!((s.pictures, s.converted), (3, 1), "one picture added to the two already there");
        assert!(job.lock().unwrap().is_none(), "the job slot is free again");
    }

    #[test]
    fn a_file_that_is_not_a_picture_is_the_converters_own_error() {
        let d = tmp("bad"); let txt = d.join("notes.txt"); std::fs::write(&txt, "x").unwrap();
        let job = Mutex::new(None);
        let e = run_conversion(&job, &opts(&txt, &d.join("o"), false), |_| {}).unwrap_err();
        assert!(e.contains("not a picture"), "{e}");
        let e = run_conversion(&job, &opts(&d.join("missing"), &d.join("o"), false), |_| {}).unwrap_err();
        assert!(e.contains("no such file"), "{e}");
    }

    #[test]
    fn a_picture_that_cannot_be_read_is_skipped_and_the_rest_still_converts() {
        let d = tmp("skip"); let src = d.join("in"); std::fs::create_dir(&src).unwrap();
        std::fs::write(src.join("broken.png"), b"not a picture at all").unwrap(); make_png(&src.join("fine.png"), 300, 200);
        let job = Mutex::new(None); let mut skipped = Vec::new();
        let s = run_conversion(&job, &opts(&src, &d.join("out"), false), |v| if v["event"] == "skip" { skipped.push(v["file"].as_str().unwrap().to_string()) }).unwrap();
        assert_eq!((s.converted, s.skipped, s.pictures), (1, 1, 1)); assert_eq!(skipped, ["broken.png"]);
    }

    #[test]
    fn a_stop_request_ends_the_run_and_only_one_runs_at_a_time() {
        let d = tmp("stop"); let src = d.join("in"); std::fs::create_dir(&src).unwrap();
        for i in 0..6 { make_png(&src.join(format!("p{i}.png")), 1200, 900); }
        let job = Arc::new(Mutex::new(None::<Arc<AtomicBool>>));
        let (j2, o) = (job.clone(), opts(&src, &d.join("out"), false));
        let t = std::thread::spawn(move || { let j3 = j2.clone(); run_conversion(&j2, &o, move |v| { if v["event"] == "plan" { assert!(stop_job(&j3), "the flag is registered while it runs"); } }) });
        let r = t.join().unwrap();
        assert_eq!(r.unwrap_err(), "Stopped.");
        assert!(!stop_job(&job), "nothing left to stop");
        *job.lock().unwrap() = Some(Arc::new(AtomicBool::new(false)));
        assert!(run_conversion(&job, &opts(&src, &d.join("o2"), false), |_| {}).unwrap_err().contains("already running"));
    }

    #[test]
    fn the_options_follow_the_format() {
        let o = |levels, upscale, merge, f: &str, q, fq, sort: &str, old| convert_options("/in", "/Volumes/TauV/Assets/tau_gallery/common", levels, upscale, merge, f, q, fq, sort, old);
        let a = o(true, false, true, "v0", 92, 90, "name", false).unwrap();
        assert_eq!((a.out.to_str().unwrap(), a.card_layout, a.format_v1, a.levels, a.merge, a.upscale), ("/Volumes/TauV", true, false, true, true, false));
        let b = o(true, true, false, "v1", 85, 70, "taken", false).unwrap();
        assert_eq!((b.format_v1, b.levels, b.quality, b.fit_quality, b.upscale, b.sort.as_str()), (true, false, 85, 70, true, "taken"), "zoom levels mean nothing for version 1");
        let c = o(false, false, false, "v0", 50, 50, "name", false).unwrap(); assert_eq!((c.quality, c.fit_quality), (92, 90), "the qualities are not used for version 0");
        assert!(o(false, false, false, "v2", 92, 90, "name", false).unwrap_err().contains("Unknown format"));
        assert!(o(false, false, false, "v1", 92, 90, "size", false).unwrap_err().contains("Unknown sort"));
        let e = o(false, false, false, "v1", 92, 90, "taken", true).unwrap(); assert!(!e.index_v1, "the old index has no folders");
        for (q, fq) in [(0, 90), (101, 90), (92, 0), (92, 101)] { assert!(o(false, false, false, "v1", q, fq, "name", false).unwrap_err().contains("1 to 100"), "{q} {fq}"); }
    }

    #[test]
    fn a_version_1_picture_converts_without_python() {
        let d = tmp("v1"); let src = d.join("in"); std::fs::create_dir(&src).unwrap(); let out = d.join("out");
        make_png(&src.join("big.png"), 1000, 900);
        let job = Mutex::new(None);
        let o = convert_options(src.to_str().unwrap(), out.to_str().unwrap(), false, false, false, "v1", 92, 90, "name", false).unwrap();
        let s = run_conversion(&job, &o, |_| {}).unwrap();
        assert_eq!((s.pictures, s.converted, s.skipped), (1, 1, 0));
        let f = std::fs::read(out.join("Assets/tau_gallery/common/photos/big.tgy")).unwrap();
        assert_eq!(&f[0..4], b"TGY\0"); assert_eq!(u16::from_le_bytes([f[4], f[5]]), 1, "a version 1 container");
        run_conversion(&job, &opts(&src, &d.join("out0"), false), |_| {}).unwrap();
        let g = std::fs::read(d.join("out0/Assets/tau_gallery/common/photos/big.tgy")).unwrap();
        assert_eq!(u16::from_le_bytes([g[4], g[5]]), 0, "version 0 stays version 0");
    }

    #[test]
    fn an_output_inside_the_cards_tree_means_the_cards_root() {
        assert_eq!(normalize_output("/Volumes/TauV/Assets/tau_gallery/common"), "/Volumes/TauV");
        assert_eq!(normalize_output("/Volumes/TauV/Assets/tau_gallery/common/"), "/Volumes/TauV");
        assert_eq!(normalize_output("/Volumes/TauV/Assets/tau_gallery/common/photos"), "/Volumes/TauV");
        assert_eq!(normalize_output("/Volumes/TauV/Assets"), "/Volumes/TauV/Assets", "an Assets folder alone is left as it is");
        assert_eq!(normalize_output("/Volumes/TauV/"), "/Volumes/TauV");
        assert_eq!(normalize_output("/Users/me/Documents/Tau Gallery"), "/Users/me/Documents/Tau Gallery");
        assert_eq!(normalize_output("/Assets/tau_gallery"), "/");
        assert_eq!(normalize_output("E:\\Assets\\tau_gallery\\common"), "E:\\", "a drive root keeps its backslash");
        assert_eq!(normalize_output("E:\\Cards\\TauV\\Assets\\tau_gallery\\common\\photos"), "E:\\Cards\\TauV");
    }

    #[test]
    #[cfg(not(windows))]
    fn volumes_lists_folders_but_not_hidden_ones_or_files_and_reads_free_space() {
        let d = tmp("vols"); std::fs::create_dir(d.join("CARD")).unwrap(); std::fs::create_dir(d.join(".hidden")).unwrap(); std::fs::write(d.join("file.txt"), "x").unwrap();
        let v = volumes_in(&[d.to_str().unwrap(), "/no/such/root"]);
        assert_eq!(v.len(), 1); assert_eq!(v[0].name, "CARD"); assert!(v[0].free_gb > 0.0, "free space {}", v[0].free_gb);
    }
}
