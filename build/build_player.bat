@echo off

call .\set_vars.bat

if not exist "..\.build" mkdir "..\.build"

pushd ..\.build
cl %INCLUDES% %OPTIONS% %* /utf-8 d3d9.lib ^
ole32.lib winmm.lib comdlg32.lib user32.lib opusfile.lib ^
ogg.lib opus.lib FLAC.lib msvcrt.lib freetype.lib ^
samplerate.lib ^
..\code\third_party\*.c ..\code\third_party\*.cpp ..\code\third_party\misc\freetype\*.cpp ^
..\code\player\*.cpp ..\code\player\decoders\*.cpp /Fe:..\data\Bin\Verata.exe %LINKER_OPTIONS%
popd

@echo on
