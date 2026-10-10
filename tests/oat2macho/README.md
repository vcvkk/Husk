Two arm64 oat files, for checking `tools/oat2macho` against dyld without
building ART first: `tests/dvm/Hello.java` and `tests/dvm/Collections.java`,
compiled by dex2oat (patches 0003 and 0004: 16 KiB layout, arm64 on x86_64)
against an arm64 boot image. `.github/workflows/darwin.yml` converts them,
signs them and loads them on macOS arm64 with `tools/oat-dlopen-test.c`.
