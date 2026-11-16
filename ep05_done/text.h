#pragma once

#include "framework/canvas.h"

// 8×8 ドットの文字を描く関数
// プロファイラーやメモリアロケーターのシリーズでも、このファイルと text.cpp をそのまま使う
// Functions for drawing 8×8 pixel text.
// The profiler and memory allocator series also use this file and text.cpp as they are.

// 文字を並べるときの幅（ピクセル）。scale 倍して使う
// The spacing between characters, in pixels. Multiply it by scale when using it.
const int text_char_width = 8;   
// 改行で下がる高さ（ピクセル）。文字の高さ 8 に、行の間の 1 を足した値
// The line height in pixels: the 8-pixel character height plus 1 pixel between lines.
const int text_line_height = 9;  

// 文字を1つ描く。(x, y) は文字の左上、scale は拡大の倍率（1 なら 8×8 ピクセル）
// 文字の点だけを打ち、背景は塗らない。表にない文字は '?' になる
// Draw one character. (x, y) is its top-left corner, and scale is the enlargement factor (1 means 8×8 pixels).
// Draw only the character's pixels; do not fill the background. Characters not in the table become '?'.
void draw_char(Canvas& canvas, int x, int y, char c, Color color, int scale);

// 文字列を、(x, y) から右へ並べて描く。'\n' が来たら、x に戻って次の行へ進む
// Draw a string from (x, y), moving to the right. When '\n' appears, return to x and move to the next line.
void draw_text(Canvas& canvas, int x, int y, const char* text, Color color, int scale);
