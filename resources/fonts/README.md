# UI fonts

The windowed debugger embeds subsetted glyphs from:

- JetBrains Mono (OFL 1.1), from the JetBrains Mono project.
- Inter (OFL 1.1), from the Inter project.

The full font files and license texts are kept here for regeneration. The
subset files contain Latin-1 (`U+0020`–`U+00FF`) and no layout features.

Regenerate the subset files (requires `pyftsubset` from fonttools):

```text
bash subset.sh
```

Regenerate `src/resources/embedded_fonts.cpp`:

```text
python3 embed_fonts.py
```

The generated C++ file is committed so normal builds do not need Python or
fonttools.
