# etsuko

*Notice:*\
**This repository is unlicensed on purpose. You may not use, copy, modify, or
distribute this code without permission.**\
**I do not plan on accepting contributions. Please get in contact through
GitHub or *info at wooby dot moe* before you waste your time writing code and
opening a pull request.**
***

etsuko is a shitty karaoke program written by reinventing the wheel in
unnecessary ways, using SDL2 with OpenGL, some stb libs and C.
It displays lyrics in real time as the song plays, along with a rudimentary
interface that mimics a music player.\
My main source of inspiration/plagiarism is Apple Music's fullscreen player.

I may add that I have no idea what I'm doing with the UI itself, nor with
OpenGL, so the program might be a little janky and awkward. The main
purpose of its existence is for my own enjoyment (I like lyrics) and to
sometimes send it to my friends (although they don't like it nearly as much
as I do).

A publicly viewable demo is hosted [here](https://tl.wooby.moe/karaoke/v2)
using emscripten to target wasm.\
Check the console if it takes a while to load to see if there are any errors.
Also, it expects support for WebGL 2/OpenGL ES 3.0, or else it'll fail to run.\
**If you represent an artist and would like your song taken down from
the website, you may also contact me via the email above.**\
**I make no money out of this whatsoever nor the site shows any ads.
All audio files were acquired legally through iTunes, and lyrics are either
my own translations or the original obtained from public sources.**

## Build instructions

This project uses CMake, so to build it on your local machine targeting the
desktop configuration (not wasm), you can run:

```bash
# Configure using the desktop-release preset
cmake --preset desktop-release

# Build the project
cmake --build --preset desktop-release

# Change working dir and run the program
cd ./build/desktop-release
./etsuko
```

**Requirements**
- CMake
- Ninja
- A C compiler (preferably clang)
- OpenGL (probably already included in your system)
- GLEW, SDL2 and ICU dev libraries

**Upon running, the program will probably fail and close because it needs a song
to play. It uses a custom format described below.**

## Playing songs

You can change the default file in config.c. All files in the desktop build
are expected to be inside the assets/ folder, which is automatically copied
on build and is composed of the contents inside the assets/ (tracked by git)
and assets_dbg/ (not tracked by git) in the root directory.

In general, a song needs 3 files to function properly:
- song_name.txt file describing the song
- album_art.jpg image containing the album art, specified inside the .txt
- song_audio.mp3 audio file also specified inside the .txt

This data is contained inside a header portion of the file and includes
other metadata. A full list of options (although not all of them are
currently used) can be found in song.c.

The structure of the file is as follows:

```
name=My song
albumArt=img/album.jpg
filePath=files/song.mp3
artist=John Doe
album=Album
alignment=center
offset=0.3
bgColor=972566
bgColorSecondary=000000
bgType=simpleGradient
#timings
0:00
0:05
0:13.1
...
#lyrics

First line of the song
And the second one#alignment=right
...
#ass
Dialogue: 0,0:00:00.00,0:00:05.75,Default,,0,0,0,,
Dialogue: 0,0:00:05.75,0:00:13.10,Default,,0,0,0,,First line of the song
Dialogue: 0,0:00:13.10,0:00:17.00,Default,,0,0,0,,And the second one#alignment=right
...
```

Notice that all files are expected to be inside the assets/ folder during
runtime, and relative paths (e.g. files/) only work in the wasm target, and
you need to define a CDN_BASE_PATH pointing to a remote server to retrieve
these files from at runtime.

You may use *either* the combination of #lyrics and #timings (timings
must come before lyrics) *OR* the #ass section which should be filled with
*only* the [Events] part of a .ass file (generated using Aegisub, for example)
from the second line onwards (ignoring the one that starts with Format:).

Only timing and line content information are used from the .ass format, so
any modifiers that can be applied to an individual line or the player itself
are custom-made and handled separately (the full list can be seen in song.c).

Using #timings is the simplest way and can be done by hand, and you can
specify a custom offset to be applied during runtime if the timing as a
whole is a little off.

It is generally expected to have an empty newline at the end of the file,
or you may see an error.

With the file created in the right format and pointed at in the config.c
source file, upon rebuilding you may see the playback.

If your song needs a different font for displaying special characters (e.g.
CJK or another non-latin language), or if you just want to use a font other
than the default, a *fontOverride=* can be specified in the header as well.

***
This repository includes code in the public domain from the following projects:
* nothings/stb: https://github.com/nothings/stb
* lieff/minimp3: https://github.com/lieff/minimp3

Also includes shaders written by Inigo Quilez, released under Creative Commons Attribution-NonCommercial-ShareAlike
3.0 Unported License ([original](https://www.shadertoy.com/view/wdyczG)), slightly modified to work in the program's context and under the selected OpenGL version:
* contrib/am gradient.frag.glsl
* contrib/cloud gradient.frag.glsl
