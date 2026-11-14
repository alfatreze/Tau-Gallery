# Tau Gallery

<img width="400" height="360" alt="20261114_022101" src="https://github.com/user-attachments/assets/b3b89f90-f357-4e1a-8b28-e34ed9548dd0" />
<img width="800" height="720" alt="20261114_022121" src="https://github.com/user-attachments/assets/a42b7f17-764c-431f-91da-00352ed44164" />
<img width="400" height="360" alt="20261113_085759" src="https://github.com/user-attachments/assets/f021955d-24da-4e0b-870f-5682cceefbe7" />


A photo gallery core for the Analogue Pocket: 
- Custom jpeg like codec for extreme performance (hardware decoded)
- Any image will open at ~250ms
- 800x600 Resolution
- Fit screen of full zoom with panning and navigator
- Ultra large image support (maximum tested 400+ megapixel)
- Dithering for 15bit analogue screen
- Histograms and EXIF
- Image rotation and color LUT effects
- MacOS encoding app and python tool, more to come in the future


## Build and package

```bash
export PATH="$PWD/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin:$PATH"     # xPack riscv-none-elf-gcc 15.2.0-1
python3 tools/package_dev_build.py --number 901 --variant player2 --build-flags DIAG=1 \
    --rbf <raw ap_core.rbf> --rbf-sha256 <its sha256>
python3 tools/install_dev_core.py work/diagnostics/tau-dev-901/pocket --card /Volumes/Pock          # dry run; --yes to write
```

The licence is Apache-2.0 (`LICENSE`). The MIT license of the upstream project and of the earlier Tau files is kept in `LICENSE-MIT`, with the attribution in `NOTICE.md` and `NOTICE`.
