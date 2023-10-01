# Verata Music Player
Lightweight music player for Windows. In early development so likely buggy.

# Supported codecs
- opus
- wav
- mp3
- flac

# Planned Features
- Support for other codecs such as aac and m4a
- Display track images
- Tag editing
- Visualizers
- Loudness normalization
- Playback statistics
- Playing from external sources
- Multiple libraries

# Building from source
The following dependencies and their dependencies are required to build Verata:
- opusfile: https://github.com/xiph/opusfile
- opus: https://github.com/xiph/opus
- flac: https://github.com/xiph/flac
- ogg: https://github.com/xiph/ogg
- libsamplerate: https://github.com/libsndfile/libsamplerate
- freetype: https://gitlab.freedesktop.org/freetype/freetype

These can all be easily installed using vcpkg.

## Steps
- If it these folders not exist, create data/Bin and code/external in the project directory.
- Place .dll binaries from dependencies into the data/Bin folder
- Place .lib and .h files for dependencies into the code/external folder
- cd into the "build" folder in a command prompt
- Call vcvars64.bat (You can find this in [your visual studio installation]/VC/Auxiliary/Build])
- run .\build_debug.bat to build with debug symbols, or .\build_release.bat for a release build.

The resulting binary is output to data/Bin/Verata.exe
