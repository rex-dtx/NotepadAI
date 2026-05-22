@echo off
set ICON_SIZES=16 32 48 64 128 256

for %%s in (%ICON_SIZES%) do (
    echo Exporting %%sx%%s PNG...
    "C:\Program Files\Inkscape\bin\inkscape.exe" NotepadAI.svg -w %%s -h %%s --export-type=png --export-filename=nn%%s.png
)

".\ImageMagick\magick" convert -background transparent nn16.png nn32.png nn48.png nn64.png nn128.png nn256.png NotepadAI.ico

for %%s in (%ICON_SIZES%) do del nn%%s.png

"C:\Program Files\Inkscape\bin\inkscape.exe" NotepadAI.svg --export-type=png --export-dpi=96 --export-background-opacity=0
copy NotepadAI.png ..\src\NotepadAI\icons\NotepadAI.png
