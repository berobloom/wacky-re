# original/

The untouched game files (from the disc or the installer). Never edit them;
the harness works on a copy in `.work/dos/C`.

```sh
7z x -ooriginal "<image>.iso"          # or copy the installed directory
(cd original && sha256sum * > SHA256SUMS)
```

Check before trusting a result: `(cd original && sha256sum -c SHA256SUMS)`.
The game files are not committed (`.gitignore`); `SHA256SUMS` is.
