"""TGY encoder for pixel sources (NOT normative: any encoder that produces valid coefficients is conforming): RGB pixels to quantised DCT coefficients in the form the rest of the chain uses (a jpegcoef.Jpeg-like object:
components with zigzag coefficient arrays and quantisation tables), so tgy_recon, the symbol layer and the container take it exactly as they take a JPEG's coefficients. Fixed quality, the JPEG standard tables scaled
by the libjpeg formula, 4:2:0 or 4:4:4 or grey, edge replication into partial blocks. No rate-distortion optimisation yet (the lever named in docs/M3_STREAMING_NOTE.md section 14)."""
import numpy as np
import jpegcoef

STD_LUMA = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56, 14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
STD_CHROMA = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99, 47, 66, 99, 99, 99, 99, 99, 99] + [99] * 32

def scaled_table(base, quality):
    """libjpeg's jpeg_quality_scaling and the table scaling, in natural order -> returned in ZIGZAG order."""
    q = max(1, min(100, quality)); s = 5000 // q if q < 50 else 200 - 2 * q
    nat = [max(1, min(255, (b * s + 50) // 100)) for b in base]
    return [nat[jpegcoef.ZZ[k]] for k in range(64)]

_C = np.array([[np.sqrt((1 if u == 0 else 2) / 8) * np.cos((2 * x + 1) * u * np.pi / 16) for x in range(8)] for u in range(8)])

def _fdct_quantise(plane, qzz):
    """plane: float samples [bh*8, bw*8] (already level shifted by -128); returns int16 [bh, bw, 64] zigzag coefficients."""
    h, w = plane.shape; bh, bw = h // 8, w // 8
    blocks = plane.reshape(bh, 8, bw, 8).transpose(0, 2, 1, 3)                                             # [bh, bw, 8, 8]
    f = np.einsum("ux,hwxy,vy->hwuv", _C, blocks, _C)                                                       # F = C X C^T (row = vertical frequency)
    q = np.zeros(64); q[jpegcoef.ZZ] = qzz; f = f.reshape(bh, bw, 64) / q
    r = np.sign(f) * np.floor(np.abs(f) + 0.5)                                                              # round half away from zero
    return r[..., jpegcoef.ZZ].astype(np.int16)

def _pad(plane, h, w):
    ph, pw = plane.shape
    return np.pad(plane, ((0, h - ph), (0, w - pw)), mode="edge")

def encode_rgb(rgb, quality=90, subsampling=2):
    """rgb: uint8 [h, w, 3] (or [h, w] for grey). subsampling: 2 = 4:2:0, 0 = 4:4:4 (ignored for grey). Returns a jpegcoef.Jpeg-like object."""
    a = np.asarray(rgb); grey = a.ndim == 2; h, w = a.shape[:2]; j = jpegcoef.Jpeg(); j.width, j.height, j.progressive, j.restart_interval, j.quant, j.components = w, h, False, 0, {}, []
    qy, qc = scaled_table(STD_LUMA, quality), scaled_table(STD_CHROMA, quality)
    if grey: planes = [a.astype(np.float64)]
    else:
        r, g, b = [a[..., i].astype(np.float64) for i in range(3)]
        y = 0.299 * r + 0.587 * g + 0.114 * b; cb = -0.168736 * r - 0.331264 * g + 0.5 * b + 128; cr = 0.5 * r - 0.418688 * g - 0.081312 * b + 128; planes = [y, cb, cr]
    hf = 2 if (not grey and subsampling == 2) else 1; mcu = 8 * hf
    mw, mh = -(-w // mcu), -(-h // mcu)
    for k, p in enumerate(planes):
        c = jpegcoef.Comp(k + 1, hf if k == 0 else 1, hf if k == 0 else 1, 0 if k == 0 else 1)
        if k > 0 and hf == 2:                                                                                 # 2 x 2 box average of the (edge-replicated to even size) chroma plane
            p = _pad(p, p.shape[0] + (p.shape[0] & 1), p.shape[1] + (p.shape[1] & 1)); p = (p[0::2, 0::2] + p[1::2, 0::2] + p[0::2, 1::2] + p[1::2, 1::2]) / 4.0
        c.blocks_w, c.blocks_h = mw * c.h, mh * c.v; p = _pad(p, c.blocks_h * 8, c.blocks_w * 8)
        c.coef = _fdct_quantise(p - 128.0, qy if k == 0 else qc); j.components.append(c)
    j.quant[0] = np.array(qy)
    if not grey: j.quant[1] = np.array(qc)
    return j
