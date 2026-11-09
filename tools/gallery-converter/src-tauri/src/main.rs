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
struct PyStatus { ok: bool, path: String, version: String, pillow: String, message: String }

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
    let out = Command::new(path).args(["-c", "import sys, PIL; print(sys.version.split()[0]); print(PIL.__version__)"]).output().ok()?;
    if !out.status.success() { return None; }
    let text = String::from_utf8_lossy(&out.stdout).to_string();
    let mut lines = text.lines();
    let version = lines.next()?.to_string();
    let pillow = lines.next()?.to_string();
    Some(PyStatus { ok: true, path: path.to_string(), version, pillow, message: String::new() })
}

#[tauri::command]
fn python_status(user: Option<String>) -> PyStatus {
    for c in python_candidates(user.as_deref()) { if let Some(s) = probe_python(&c) { return s; } }
    PyStatus { ok: false, path: String::new(), version: String::new(), pillow: String::new(),
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

#[tauri::command]
async fn convert(app: AppHandle, input: String, output: String, levels: bool, upscale: bool, merge: bool, python: Option<String>) -> Result<Summary, String> {
    let py = python_status(python);
    if !py.ok { return Err(py.message); }
    let script = converter_script(&app)?;
    let mut args: Vec<String> = vec![script.to_string_lossy().into_owned(), input, output, "--card-layout".into()];
    if levels { args.push("--levels".into()); }
    if upscale { args.push("--upscale".into()); }
    if merge { args.push("--merge".into()); }
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
        .invoke_handler(tauri::generate_handler![python_status, scan, default_output, convert, cancel, reveal])
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
}
