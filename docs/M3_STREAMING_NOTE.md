# Decision note 2026-11-10: compressed formats first, whole picture resident in SDRAM, pan and zoom from memory

Owner decisions (2026-11-10): (1) tuning the raw-tile pan from the card is not worth more effort, because the goal is to load the owner's own format (TGY, then TGX) and also JPEG and PNG directly, even if the load is slower; (2) the format work comes first, TGY before TGX; (3) the compressed picture is streamed into SDRAM (PSRAM as a cache at most) once, and pan and zoom decode from memory instead of streaming from the card. This note records what that means. Plan sections: 6.5 (TGY), 6.6 (TGX), 8 (milestones, M3 and M4); M2 design: `docs/M2_DESIGN.md`.

## 0. Priorities (owner, 2026-11-10)
1. **Essential, the critical goal: TGY and TGX in hardware** (both decoders, and the host encoders that feed them). TGX is no longer "optional later": it is part of the essential goal, TGY first.
2. **Nice to have, for the user's quality of life (reading straight from a card): JPEG direct and PNG direct**, even if their load is slower than a converted file. They follow the hardware TGY and TGX work and reuse its pieces where they can (JPEG shares the TGY back end; PNG is a separate sequential engine).
3. **Much later: camera RAW** (loading raw files from a camera). Not planned here beyond this line.

## 1. Vocabulary (what is meant, so the code and the plan agree)
- **TGY**: the native lossy photo format. Tiled (128 x 128, independent tiles), a JPEG-class DCT with quantisation and an adaptive binary range coder; resolutions 1/8, 1/4, 1/2, 1/1 by DCT-domain scaling at no extra storage; band-major layout (DC, then low AC, then high AC) so a coarse view is a short contiguous read. JPEG sources are recompressed from their coefficients (no added loss); other sources are encoded once at a preset (q95, q92 4:4:4 default, q88 4:2:0). Decoder: range decoder, dequantise, IDCT, colour, chroma upsample.
- **TGX**: the lossless tier for screenshots, text and graphics (where DCT rings): predictor plus context plus adaptive range coding of the residual (the same range coder). Chosen per tile by the mode byte of the same container. Part of the essential goal (section 0), built after TGY.
- **The container** (what the files are today): `.tgy` files hold the header, a thumbnail and levels whose payload mode is RAW (mode 0). TGY and TGX payloads are later modes of the same container; the extension and the codec share the name `TGY`, and the docs say "TGY mode" for the payload.
- **JPEG direct** (plan M4): baseline and progressive, sharing the TGY back end, only the entropy front end differs. **PNG direct** was not in the plan (it converted PNG and other pixel sources on the PC); the owner decided 2026-11-10 that it is wanted, as a nice to have after TGY and TGX. It is a separate engine (inflate and the five PNG filters, a sequential stream): the whole picture is decoded once into raw pixels in SDRAM and panned from there (a 12 MP picture is 36 MB, so a very large PNG needs a decode into tiles or a size limit, to be decided with the first measurements).

## 2. Why a resident compressed picture
The raw pan is limited by the card (about 38 ms for a 48 KB tile, B-1024 to B-1035). With the compressed file resident in SDRAM the card is read once per picture (the DC band first, so a coarse picture appears early and refines while the rest loads), and every pan and zoom step reads SDRAM; the limits become decoder throughput and SDRAM bandwidth. The tile cache API (request `(image, level, tx, ty)`, get a slot or pending) does not change, only its backing store. This needs a compressed format: a raw 1:1 level of a 12 MP photo is 36 MB.

## 3. Sizes and load times of the five test photos at FULL resolution (measured sizes, estimated times)
`python3 tools/gallery/size_table.py ../test-images/unsplash` (JPEG re-encodes with optimised Huffman at the picture's own resolution, a second-generation proxy: a range-coded TGY is smaller; times at the 1.55 MB/s measured for large commands, docs/M0_DESIGN.md 6e).

| Photo | Pixels | Original JPEG | q95 4:4:4 | q92 4:4:4 | q88 4:2:0 | Raw RGB888 | Load q92 | Load raw |
|---|---|---|---|---|---|---|---|---|
| c6227db76b6e | 2560x1705 (4.4 MP) | 2.6 MB | 2.5 MB | 2.1 MB | 1.5 MB | 13.1 MB | 1.4 s | 8 s |
| 56623f02e42e | 3506x2329 (8.2 MP) | 2.5 MB | 2.1 MB | 1.5 MB | 1.0 MB | 24.5 MB | 1.0 s | 16 s |
| 3b5ec3a7fe05 | 6000x3575 (21.4 MP) | 6.6 MB | 6.0 MB | 4.5 MB | 2.9 MB | 64.3 MB | 2.9 s | 42 s |
| b586d89ba3ee | 3648x5472 (20.0 MP) | 3.6 MB | 3.6 MB | 2.8 MB | 2.1 MB | 59.9 MB | 1.8 s | 39 s |
| 21bda4d32df4 | 4878x3252 (15.9 MP) | 3.2 MB | 3.1 MB | 2.5 MB | 1.9 MB | 47.6 MB | 1.6 s | 31 s |

Reading it: every picture is 1.0 to 6.6 MB compressed, so it fits SDRAM (64 MB) many times over, and loads in 1 to 4 s; a raw 21 MP picture (64 MB) does not even fit. For JPEG sources the plan's measured floor (`jpegtran -arithmetic`, mean 0.933 of the original, plan 13.3) gives about 2.3 to 6.1 MB, or 1.5 to 3.9 s at the same rate. The first view arrives much earlier: a DC-only (1/8 scale) view is about 4 to 9% of the file in the plan's progressive-JPEG measurement (0.1 to 0.4 MB, under 0.3 s).

## 4. What stays and what goes
- **Stays (format-independent):** the viewer and landing path, the container and index, the resident window and follow rule, the loader bar, the map, the settings and screens, the Talos tile copy (still how decoded tiles get into the plane).
- **Goes or waits:** tuning built around the raw 48 KB tile cost (pan cap per frame, window sizes), the card-streaming pan as the final design (it stays as the RAW fallback), the hardware map overlay (little value before the decoder exists).

## 5. Work order
1. **Host, no hardware:** the TGY encoder and reference decoder (Python first, as the plan's G4a and G5 benchmarks need), quality presets, JPEG coefficient recompression, the exact size and speed numbers; the range coder as a bit-exact reference with test vectors.
2. **SDRAM-resident store (firmware and RTL interface):** whole-file landing in 128 KB commands (the largest measured), the band-major progressive load, and the tile cache API backed by SDRAM; measure reads above 128 KB and fragmented files.
3. **RTL decoder** (range decoder front end, dequantise, IDCT, colour, upsample), on the shared port structure (the plan: a write mux with the landing path); two-seed fit; M10K budget (264 of 308 used by the stripped M0 core, 86%).
4. **TGX** (host encoder and reference decoder with the same range coder, then the hardware decoder): essential.
5. **JPEG direct** on the TGY back end (M4), then **PNG direct** (inflate, filters, a decode into SDRAM): nice to have, in that order. Camera RAW much later.

## 6. Open measurements
Reads above 128 KB from a multi-megabyte file and from a fragmented file; the decoder's throughput against SDRAM (the plan's TGX estimate: about 0.5 ms for a 128 x 128 tile); the Talos copy rate (the ZOOM COPY row of the Info page from DEV 007 onward); SDRAM port sharing between a background load, the decoder write and the scan-out; the PSRAM window size (not checked here).
