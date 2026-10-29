# SCODE

A tiny, dependency-free terminal editor that live-encodes and decodes text into a custom numeric cipher — written as a single portable C file with a vim-style modal interface.

Type text, watch it get converted in real time, and write the result straight to a file. No libraries beyond the C standard library and the platform's native terminal API (`termios` on POSIX, the Win32 console API on Windows).

## Features

- **Modal editing** — Insert mode for typing, Normal mode for commands, just like vi/vim.
- **Live dual-pane preview** — input and converted output are redrawn on every keystroke.
- **Two-way conversion** — switch between *encode* (plain text → SCODE) and *decode* (SCODE → plain text) at any time.
- **UTF-8 aware** — multi-byte input is decoded on read and re-encoded on output (`put8`), so non-ASCII codepoints survive the round trip where a mapping exists.
- **Single file, zero dependencies** — builds with any C compiler, no external libs.
- **Cross-platform** — one source tree (`#ifdef _WIN32`) targets POSIX terminals and the Windows console.
- **Write-and-exit** — `:w [file]` dumps the current output buffer to disk.

## How the encoding works

Each supported character maps to an integer code; encoded output is those integers written as decimal, joined with `_` for consecutive non-whitespace characters. Spaces, tabs, and newlines are passed through literally (tabs are normalized to spaces).

| Input class | Rule | Range |
|---|---|---|
| Uppercase `A`–`Z` | `2*(c-'A')+1` | 1, 3, 5, … 51 (odd) |
| Lowercase `a`–`z` | `2*(c-'a')+2` | 2, 4, 6, … 52 (even) |
| Digits `0`–`9` | `2*(c-'0')+1` | 1, 3, 5, … 19 (odd) |
| Punctuation / symbols | fixed lookup table (`pmap`) | 57–90 |
| Space / tab / newline | passed through unchanged | — |
| Anything else | dropped | — |

The punctuation table covers: `! " $ € ^ & * ( ) < > , ? / ; : ~ # { } ' @ - _ + = \` £ % . [ ] \ |`

Decoding reads back digit runs and reverses the mapping: odd numbers 1–52 become uppercase letters, even numbers 2–52 become lowercase, and 57–90 are looked up in the punctuation table.

### Example

Encoding `Hi!` produces:

```
15_18_57
```

Decoding `15_18_57` reproduces `Hi!`.

### Known limitation

Digits and uppercase letters share the same formula (`2*n+1`), so `'0'`–`'9'` collide with `'A'`–`'J'` (e.g. both `'A'` and `'0'` encode to `1`). Decoding is therefore lossy for digits — a decoded `1` always comes back as `A`, never `0`. If you need digits to round-trip, give them a disjoint code range.

## Keybindings

| Mode | Key | Action |
|---|---|---|
| Normal | `i` / `a` | Enter Insert mode |
| Normal | `:` | Enter Command mode |
| Insert | `Esc` | Return to Normal mode |
| Insert | `Backspace` | Delete last character |
| Insert | any char | Append to buffer |

## Commands (`:`)

| Command | Effect |
|---|---|
| `:s` | Switch to **encode** mode, clear the buffer, return to Insert |
| `:d` | Switch to **decode** mode, clear the buffer, return to Insert |
| `:w [file]` | Write the current output to `file` (default: `scode.txt`) and exit |
| `:q` | Quit without saving |

## Build from source

Source lives in `src/main.c`.

**Linux / macOS**
```bash
gcc -O2 -o scode src/main.c
./scode
```

**Windows (MSVC)**
```bat
cl /O2 src\main.c /Fe:SCODE.EXE
```

**Windows (MinGW)**
```bash
gcc -O2 -o SCODE.EXE src/main.c
```

No extra `-l` flags needed — everything used (`conio.h`, `windows.h` on Windows; `termios.h`, `unistd.h` on POSIX) ships with the toolchain.

## Prebuilt binaries

| Platform | Binary |
|---|---|
| macOS (Apple Silicon) | `Downloads/Apple/MacOS Apple/SCODE` |
| macOS (x86_64) | `Downloads/Apple/MacOS x86_64/SCODE` |
| Linux (32-bit) | `Downloads/Linux/Linux 32/SCODE` |
| Linux (64-bit) | `Downloads/Linux/Linux 64/SCODE` |
| Windows (32-bit) | `Downloads/Windows/Windows 32/SCODE.EXE` |
| Windows (64-bit) | `Downloads/Windows/Windows 64/SCODE.EXE` |

## Requirements

- A terminal that honors ANSI escape codes (`\x1b[2J\x1b[H` for screen clear). On Windows, `term_on()` enables virtual terminal processing automatically, so no manual setup is needed.
- Input buffer is capped at `MAXN` (65536) characters.

## License

No license file is currently included — add a `LICENSE` to the repo root to make reuse terms explicit.
