`bitstream.rbf_r`, `tau.rom` and `tau-cold.bin` under this folder are the Tau Alpha 0.6.x build products inherited with the fork (import `12418aab`),
kept only so the host tests that build a card from dist/ still run. They are NOT Tau Gallery builds and are not paired with the current firmware
(the packager's `--rbf` is always used for a dev build). The first Tau Gallery release (tools/make_release.py) replaces them.
