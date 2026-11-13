// Tau Gallery Converter: picks a picture or a folder, runs the tested converter (tools/gallery/tgy_convert.py, bundled beside the app) and shows what it says.
// The converter writes the card's own tree (Assets/tau_gallery/common/...) into the chosen folder; the app only starts it, relays its one-JSON-event-a-line output and can stop it.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::Serialize;
use std::io::{BufRead, BufReader};
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::Mutex;
use tauri::{AppHandle, Emitter, Manager, State};

const EXT: [&str; 8] = ["jpg", "jpeg", "png", "bmp", "gif", "tif", "tiff", "webp"];

#[derive(Default)]
struct Job(Mutex<Option<Child>>);

#[derive(Serialize, Clone)]
struct PyStatus { ok: bool, path: String, version: String, pillow: String, numpy: String, message: String }

#[derive(Serialize)]
struct Scan { kind: String, name: String, pictures: usize }

#[derive(Serialize, Debug)]
struct Summary { pictures: u64, converted: u64, skipped: u64, out: String }

fn is_picture(p: &Path) -> bool {
    p.extension().and_then(|e| e.to_str()).map(|e| EXT.contains(&e.to_ascii_lowercase().as_str())).unwrap_or(false)
}

/// Every Python 3 worth trying, the user's choice first.
fn python_candidates(user: Option<&str>) -> Vec<String> {
    let mut v: Vec<String> = Vec::new();
    if let Some(u) = user { if !u.trim().is_empty() { v.push(u.trim().to_string()); } }
    for p in ["python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3", "/usr/bin/python3", "python"] { v.push(p.to_string()); }
    if let Ok(rd) = std::fs::read_dir("/Library/Frameworks/Python.framework/Versions") {
        let mut vers: Vec<PathBuf> = rd.filter_map(|e| e.ok()).map(|e| e.path()).collect();
        vers.sort(); vers.reverse();
        for d in vers { v.push(d.join("bin/python3").to_string_lossy().into_owned()); }
    }
    v
}

fn probe_python(path: &str) -> Option<PyStatus> {
    let out = Command::new(path).args(["-c", "import sys, PIL\ntry:\n import numpy; n = numpy.__version__\nexcept Exception:\n n = ''\nprint(sys.version.split()[0]); print(PIL.__version__); print(n)"]).output().ok()?;
    if !out.status.success() { return None; }
    let text = String::from_utf8_lossy(&out.stdout).to_string();
    let mut lines = text.lines();
    let version = lines.next()?.to_string();
    let pillow = lines.next()?.to_string();
    let numpy = lines.next().unwrap_or("").to_string();                                   // version 1 files (the hardware decoder's) need numpy; version 0 needs Pillow only
    Some(PyStatus { ok: true, path: path.to_string(), version, pillow, numpy, message: String::new() })
}

#[tauri::command]
fn python_status(user: Option<String>) -> PyStatus {
    for c in python_candidates(user.as_deref()) { if let Some(s) = probe_python(&c) { return s; } }
    PyStatus { ok: false, path: String::new(), version: String::new(), pillow: String::new(), numpy: String::new(),
        message: "No Python 3 with Pillow was found. Install Python 3 and run: python3 -m pip install Pillow. Or enter the full path of a Python that has it.".into() }
}

#[tauri::command]
fn scan(path: String) -> Scan {
    let p = Path::new(&path);
    let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_else(|| path.clone());
    if p.is_file() { return Scan { kind: if is_picture(p) { "file".into() } else { "unsupported".into() }, name, pictures: usize::from(is_picture(p)) }; }
    if p.is_dir() {
        let n = std::fs::read_dir(p).map(|rd| rd.filter_map(|e| e.ok()).filter(|e| e.path().is_file() && is_picture(&e.path())).count()).unwrap_or(0);
        return Scan { kind: "folder".into(), name, pictures: n };
    }
    Scan { kind: "missing".into(), name, pictures: 0 }
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
            let root = parts[..i].join("/");
            return if root.is_empty() { "/".to_string() } else { root };
        }
    }
    p.to_string()
}

#[derive(Serialize, Debug)]
struct Volume { name: String, path: String, free_gb: f64 }

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
fn free_gb(p: &Path) -> f64 {
    let Ok(o) = Command::new("df").args(["-k"]).arg(p).output() else { return -1.0 };
    String::from_utf8_lossy(&o.stdout).lines().nth(1).and_then(|l| l.split_whitespace().nth(3)).and_then(|s| s.parse::<f64>().ok()).map(|k| (k / 1048576.0 * 10.0).round() / 10.0).unwrap_or(-1.0)
}
#[tauri::command]
fn list_volumes() -> Vec<Volume> {
    let user = std::env::var("USER").unwrap_or_default();
    let media = format!("/media/{user}"); let run_media = format!("/run/media/{user}");
    volumes_in(&["/Volumes", media.as_str(), run_media.as_str()])
}
#[tauri::command]
fn normalized(path: String) -> String { normalize_output(&path) }

fn converter_script(app: &AppHandle) -> Result<PathBuf, String> {
    let mut tries: Vec<PathBuf> = Vec::new();
    if let Ok(dir) = app.path().resource_dir() { tries.push(dir.join("resources").join("tgy_convert.py")); tries.push(dir.join("tgy_convert.py")); }
    tries.push(Path::new(env!("CARGO_MANIFEST_DIR")).join("resources").join("tgy_convert.py"));
    tries.into_iter().find(|p| p.is_file()).ok_or_else(|| "The converter script is missing from the app (tgy_convert.py).".to_string())
}

/// Runs the converter and relays its events. The child sits in `job` while it runs so `cancel` can stop it. Returns the converter's own "done" summary.
fn run_converter(job: &Mutex<Option<Child>>, python: &str, args: &[String], mut on_event: impl FnMut(serde_json::Value)) -> Result<Summary, String> {
    let mut child = Command::new(python).args(args).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn().map_err(|e| format!("Could not start Python: {e}"))?;
    let stdout = child.stdout.take().ok_or("no output pipe")?;
    let mut stderr = child.stderr.take().ok_or("no error pipe")?;
    {
        let mut slot = job.lock().map_err(|_| "job lock")?;
        if slot.is_some() { let _ = child.kill(); return Err("A conversion is already running.".into()); }
        *slot = Some(child);
    }
    let err_reader = std::thread::spawn(move || { let mut s = String::new(); let _ = std::io::Read::read_to_string(&mut stderr, &mut s); s });
    let mut done: Option<Summary> = None; let mut last_error = String::new();
    for line in BufReader::new(stdout).lines().map_while(Result::ok) {
        if let Ok(v) = serde_json::from_str::<serde_json::Value>(&line) {
            match v.get("event").and_then(|e| e.as_str()) {
                Some("done") => done = Some(Summary { pictures: v["pictures"].as_u64().unwrap_or(0), converted: v["converted"].as_u64().unwrap_or(0), skipped: v["skipped"].as_u64().unwrap_or(0), out: v["out"].as_str().unwrap_or("").to_string() }),
                Some("error") => last_error = v["error"].as_str().unwrap_or("").to_string(),
                _ => {}
            }
            on_event(v);
        }
    }
    let err_text = err_reader.join().unwrap_or_default();
    let status = { let mut slot = job.lock().map_err(|_| "job lock")?; slot.take().map(|mut c| c.wait()) };
    if let Some(s) = done { return Ok(s); }
    if !last_error.is_empty() { return Err(last_error); }
    if matches!(status, Some(Ok(s)) if s.code().is_none()) { return Err("Stopped.".into()); }
    let tail: Vec<&str> = err_text.lines().rev().take(3).collect();
    let tail: String = tail.into_iter().rev().collect::<Vec<_>>().join(" ");
    Err(if tail.is_empty() { "The converter stopped without finishing.".into() } else { tail })
}

fn kill_job(job: &Mutex<Option<Child>>) -> bool {
    match job.lock() { Ok(mut slot) => match slot.as_mut() { Some(c) => c.kill().is_ok(), None => false }, Err(_) => false }
}

/// The converter's command line for one run. `format` is "v0" (the picture as raw tiles, every Tau Gallery core; `levels` adds the half and 1:1 zoom stops) or "v1" (the compressed container the hardware TGY decoder draws;
/// its zoom level is written by the converter itself for a picture larger than the screen, so `levels` means nothing there; `quality` is the quality of that level for a picture that is not a JPEG, `fit_quality` the fit level's).
fn convert_args(script: &Path, input: &str, output: &str, levels: bool, upscale: bool, merge: bool, format: &str, quality: u32, fit_quality: u32) -> Result<Vec<String>, String> {
    if format != "v0" && format != "v1" { return Err(format!("Unknown format {format}.")); }
    if !(1..=100).contains(&quality) || !(1..=100).contains(&fit_quality) { return Err("The quality is a number from 1 to 100.".into()); }
    let mut args: Vec<String> = vec!["-u".into(), script.to_string_lossy().into_owned(), input.into(), normalize_output(output), "--card-layout".into()];
    if format == "v1" { args.extend(["--format".into(), "v1".into(), "--quality".into(), quality.to_string(), "--fit-quality".into(), fit_quality.to_string()]); }
    else if levels { args.push("--levels".into()); }
    if upscale { args.push("--upscale".into()); }
    if merge { args.push("--merge".into()); }
    Ok(args)
}

/// Version 1 files (the hardware decoder's) need numpy in the chosen Python; version 0 needs Pillow only.
fn python_fits(py: &PyStatus, format: &str) -> Result<(), String> {
    if format == "v1" && py.numpy.is_empty() { return Err("The hardware-decoder format needs numpy. Install it with: python3 -m pip install numpy".into()); }
    Ok(())
}

#[tauri::command]
async fn convert(app: AppHandle, input: String, output: String, levels: bool, upscale: bool, merge: bool, format: String, quality: u32, fit_quality: u32, python: Option<String>) -> Result<Summary, String> {
    let py = python_status(python);
    if !py.ok { return Err(py.message); }
    python_fits(&py, &format)?;
    let script = converter_script(&app)?;
    let args = convert_args(&script, &input, &output, levels, upscale, merge, &format, quality, fit_quality)?;
    let handle = app.clone();
    tauri::async_runtime::spawn_blocking(move || {
        let job = handle.state::<Job>();
        let h2 = handle.clone();
        run_converter(&job.0, &py.path, &args, move |v| { let _ = h2.emit("convert-event", v); })
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
fn cancel(state: State<'_, Job>) -> bool { kill_job(&state.0) }

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
        .invoke_handler(tauri::generate_handler![python_status, scan, default_output, list_volumes, normalized, convert, cancel, reveal])
        .run(tauri::generate_context!())
        .expect("error while running Tau Gallery Converter");
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    fn tmp(name: &str) -> PathBuf { let d = std::env::temp_dir().join(format!("tgc-{}-{}", name, std::process::id())); let _ = std::fs::remove_dir_all(&d); std::fs::create_dir_all(&d).unwrap(); d }
    fn script() -> PathBuf { Path::new(env!("CARGO_MANIFEST_DIR")).join("resources").join("tgy_convert.py") }
    fn py() -> PyStatus { let s = python_status(None); assert!(s.ok, "{}", s.message); s }
    fn make_png(py: &str, path: &Path, w: u32, h: u32) {
        let code = format!("from PIL import Image; Image.new('RGB', ({w}, {h}), (200, 60, 30)).save(r'{}')", path.display());
        assert!(Command::new(py).args(["-c", &code]).status().unwrap().success());
    }

    #[test]
    fn pictures_are_recognised_by_extension() {
        assert!(is_picture(Path::new("a.JPG")) && is_picture(Path::new("b.webp")) && is_picture(Path::new("c.tiff")));
        assert!(!is_picture(Path::new("notes.txt")) && !is_picture(Path::new("noext")) && !is_picture(Path::new(".png.part")));
    }

    #[test]
    fn scan_tells_a_picture_a_folder_and_the_rest_apart() {
        let d = tmp("scan"); let p = py();
        make_png(&p.path, &d.join("one.png"), 40, 30); make_png(&p.path, &d.join("two.PNG"), 40, 30); std::fs::write(d.join("notes.txt"), "x").unwrap(); std::fs::create_dir(d.join("sub")).unwrap(); make_png(&p.path, &d.join("sub").join("deep.png"), 10, 10);
        let f = scan(d.join("one.png").to_string_lossy().into_owned()); assert_eq!((f.kind.as_str(), f.pictures), ("file", 1));
        let t = scan(d.join("notes.txt").to_string_lossy().into_owned()); assert_eq!((t.kind.as_str(), t.pictures), ("unsupported", 0));
        let dir = scan(d.to_string_lossy().into_owned()); assert_eq!((dir.kind.as_str(), dir.pictures), ("folder", 2), "subfolders are not counted");
        let m = scan(d.join("nope").to_string_lossy().into_owned()); assert_eq!((m.kind.as_str(), m.pictures), ("missing", 0));
    }

    #[test]
    fn a_user_python_is_tried_first_and_a_bad_one_falls_through() {
        assert_eq!(python_candidates(Some("/x/python")).first().map(|s| s.as_str()), Some("/x/python"));
        assert_eq!(python_candidates(Some("  ")).first().map(|s| s.as_str()), Some("python3"));
        assert!(python_status(Some("/no/such/python".into())).ok, "a bad path falls through to the next candidate");
    }

    #[test]
    fn a_folder_converts_into_the_cards_tree_and_a_single_picture_is_added_to_it() {
        let d = tmp("conv"); let p = py(); let src = d.join("in"); std::fs::create_dir(&src).unwrap(); let out = d.join("out");
        make_png(&p.path, &src.join("a.png"), 900, 600); make_png(&p.path, &src.join("b.png"), 600, 900); std::fs::write(src.join("notes.txt"), "x").unwrap();
        let job = Mutex::new(None); let mut events: Vec<String> = Vec::new();
        let args = |input: &Path, merge: bool| { let mut v = vec![script().to_string_lossy().into_owned(), input.to_string_lossy().into_owned(), out.to_string_lossy().into_owned(), "--card-layout".to_string()]; if merge { v.push("--merge".into()); } v };
        let s = run_converter(&job, &p.path, &args(&src, false), |v| events.push(v["event"].as_str().unwrap_or("").to_string())).unwrap();
        assert_eq!((s.pictures, s.converted, s.skipped), (2, 2, 0));
        assert_eq!(events, ["plan", "converted", "converted", "done"]);
        assert!(out.join("Assets/tau_gallery/common/tau-library.tdb").is_file() && out.join("Assets/tau_gallery/common/photos/a.tgy").is_file());
        let one = d.join("c.png"); make_png(&p.path, &one, 500, 500);
        let s = run_converter(&job, &p.path, &args(&one, true), |_| {}).unwrap();
        assert_eq!((s.pictures, s.converted), (3, 1), "one picture added to the two already there");
        assert!(job.lock().unwrap().is_none(), "the job slot is free again");
    }

    #[test]
    fn a_file_that_is_not_a_picture_is_the_converters_own_error() {
        let d = tmp("bad"); let p = py(); let txt = d.join("notes.txt"); std::fs::write(&txt, "x").unwrap();
        let job = Mutex::new(None);
        let e = run_converter(&job, &p.path, &[script().to_string_lossy().into_owned(), txt.to_string_lossy().into_owned(), d.join("o").to_string_lossy().into_owned()], |_| {}).unwrap_err();
        assert!(e.contains("not a picture"), "{e}");
        assert!(run_converter(&job, "/no/such/python", &[], |_| {}).unwrap_err().contains("Could not start"));
    }

    #[test]
    fn a_running_conversion_can_be_stopped_and_only_one_runs_at_a_time() {
        let p = py(); let job = Arc::new(Mutex::new(None::<Child>));
        let code = "import time,sys; print('{\"event\":\"plan\",\"files\":1,\"kept\":0}', flush=True); time.sleep(60)".to_string();
        let j2 = job.clone(); let path = p.path.clone();
        let t = std::thread::spawn(move || run_converter(&j2, &path, &["-c".to_string(), code], |_| {}));
        for _ in 0..100 { if job.lock().unwrap().is_some() { break; } std::thread::sleep(std::time::Duration::from_millis(50)); }
        assert!(job.lock().unwrap().is_some(), "the child is registered while it runs");
        assert!(run_converter(&job, &p.path, &["-c".to_string(), "print(1)".to_string()], |_| {}).unwrap_err().contains("already running"));
        assert!(kill_job(&job));
        assert_eq!(t.join().unwrap().unwrap_err(), "Stopped.");
        assert!(!kill_job(&job), "nothing left to stop");
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
    }

    #[test]
    fn volumes_lists_folders_but_not_hidden_ones_or_files_and_reads_free_space() {
        let d = tmp("vols"); std::fs::create_dir(d.join("CARD")).unwrap(); std::fs::create_dir(d.join(".hidden")).unwrap(); std::fs::write(d.join("file.txt"), "x").unwrap();
        let v = volumes_in(&[d.to_str().unwrap(), "/no/such/root"]);
        assert_eq!(v.len(), 1); assert_eq!(v[0].name, "CARD"); assert!(v[0].free_gb > 0.0, "free space {}", v[0].free_gb);
    }

    #[test]
    fn the_command_line_follows_the_format() {
        let sc = Path::new("/s/tgy_convert.py");
        let a = |levels, upscale, merge, f: &str, q, fq| convert_args(sc, "/in", "/Volumes/TauV/Assets/tau_gallery/common", levels, upscale, merge, f, q, fq).unwrap();
        assert_eq!(a(true, false, true, "v0", 92, 90), ["-u", "/s/tgy_convert.py", "/in", "/Volumes/TauV", "--card-layout", "--levels", "--merge"]);
        assert_eq!(a(false, true, false, "v0", 50, 50), ["-u", "/s/tgy_convert.py", "/in", "/Volumes/TauV", "--card-layout", "--upscale"], "the qualities are not passed for version 0");
        assert_eq!(a(true, true, true, "v1", 85, 70), ["-u", "/s/tgy_convert.py", "/in", "/Volumes/TauV", "--card-layout", "--format", "v1", "--quality", "85", "--fit-quality", "70", "--upscale", "--merge"], "zoom levels mean nothing for version 1");
        assert!(convert_args(sc, "/in", "/o", false, false, false, "v2", 92, 90).unwrap_err().contains("Unknown format"));
        for (q, fq) in [(0, 90), (101, 90), (92, 0), (92, 101)] { assert!(convert_args(sc, "/in", "/o", false, false, false, "v1", q, fq).unwrap_err().contains("1 to 100"), "{q} {fq}"); }
    }

    #[test]
    fn the_python_probe_reports_numpy_and_a_version_1_picture_converts() {
        let d = tmp("v1"); let p = py();
        if p.numpy.is_empty() { eprintln!("numpy is not installed here: the version 1 conversion is not run"); return; }
        let src = d.join("in"); std::fs::create_dir(&src).unwrap(); let out = d.join("out");
        let code = format!("from PIL import Image; import random; r = random.Random(3); im = Image.frombytes('RGB', (1000, 900), r.randbytes(1000 * 900 * 3)); im.save(r'{}', quality=90)", src.join("big.jpg").display());
        assert!(Command::new(&p.path).args(["-c", &code]).status().unwrap().success());
        let job = Mutex::new(None);
        let args = convert_args(&script(), src.to_str().unwrap(), out.to_str().unwrap(), false, false, false, "v1", 92, 90).unwrap();
        let s = run_converter(&job, &p.path, &args, |_| {}).unwrap();
        assert_eq!((s.pictures, s.converted, s.skipped), (1, 1, 0));
        let f = std::fs::read(out.join("Assets/tau_gallery/common/photos/big.tgy")).unwrap();
        assert_eq!(&f[0..4], b"TGY\0"); assert_eq!(u16::from_le_bytes([f[4], f[5]]), 1, "a version 1 container");
        let v0 = convert_args(&script(), src.to_str().unwrap(), d.join("out0").to_str().unwrap(), false, false, false, "v0", 92, 90).unwrap();
        run_converter(&job, &p.path, &v0, |_| {}).unwrap();
        let g = std::fs::read(d.join("out0/Assets/tau_gallery/common/photos/big.tgy")).unwrap();
        assert_eq!(u16::from_le_bytes([g[4], g[5]]), 0, "version 0 stays version 0");
    }

    #[test]
    fn version_1_needs_numpy_and_version_0_does_not() {
        let mut p = PyStatus { ok: true, path: "p".into(), version: "3".into(), pillow: "1".into(), numpy: String::new(), message: String::new() };
        assert!(python_fits(&p, "v1").unwrap_err().contains("numpy")); assert!(python_fits(&p, "v0").is_ok());
        p.numpy = "2.0".into(); assert!(python_fits(&p, "v1").is_ok());
    }
}
