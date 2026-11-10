# font-renderer-from-scratch

This repository contains the code for a YouTube video series on the channel "Rewheel Lab." In the series, we build a font renderer from scratch in C++ to display text on screen.

Starting with pixel-art characters drawn by hand, the series progresses to reading font files, filling outlines, and rendering Japanese text sharply even when enlarged, using SDF (signed distance field). Font files are not included. The program reads fonts installed on Windows when it runs.

Code is added for each episode as the videos are published.

## Requirements

- Visual Studio Community with the "Desktop development with C++" workload installed

That is all you need. No other tools or libraries are used.

## How to run

1. Click the green "Code" button on this page and select "Download ZIP."
2. Right-click the downloaded ZIP file and select "Extract All."
3. Open the folder for the episode you want to watch and double-click its `.sln` file.
4. When Visual Studio opens, press F5.

## If Yu Gothic is not installed

Episode 3 onward reads Yu Gothic from Windows (`C:/Windows/Fonts/YuGothR.ttc`; Episodes 11 and 12 use the bold font, `C:/Windows/Fonts/YuGothB.ttc`). On a PC without Yu Gothic, such as one running an English version of Windows, change the font path in `main.cpp` for the episode you want to watch to a font installed on that PC, such as `C:/Windows/Fonts/meiryo.ttc`. If Meiryo is not installed either, check the font file names in `C:/Windows/Fonts/`.

In the completed version and later episodes, use Ctrl+F in Visual Studio to search for `font_path`. You will find this declaration. Change only the text inside the quotes, save, and press F5.

```cpp
const char* font_path = "C:/Windows/Fonts/meiryo.ttc";
```

`ep03_start` does not have this declaration yet. When adding it at the "write the code to read the font here" location in `main()`, use the path to a font installed on your PC. The program can read `.ttf` and `.ttc` files that contain TrueType outlines. To render Japanese text such as あ, choose a font that includes Japanese characters. If a character is not supported, it is shown as a square replacement glyph. Font files are not included in this repository.

## How to navigate the folders

There are two folders for each episode:

- `epXX_start` ... the code as it is at the start of the episode. Follow along with the video and continue from here.
- `epXX_done` ... the code at the end of the episode. Compare your work with this version if something does not work.

If you get stuck in an earlier episode, you can join again from the next episode's `_start` folder. You are also free to use this as material for classes or study groups (MIT License).
