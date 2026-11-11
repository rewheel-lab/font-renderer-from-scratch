#include "font.h"

#include <cstdio>
#include <fstream>

// ここに、この回で書くものを足していく
//   read_file … ファイルを丸ごと読んで、バイトの列にする関数
//   read_u16、read_u32、read_i16 … バイトの列の offset 番目から、2バイト・4バイトの整数を読む関数
//   find_table … 表の目次から、名前（"head" など）の表の位置を探す関数（このファイルの中だけで使うので static を付ける）
//   load_font … フォントのファイルを読んで、Font に入れる関数（表の目次、head、maxp、cmap、loca、glyf の位置）
//   find_glyph_index … 文字コードから、グリフの番号を求める関数（cmap を引く）
//   load_glyph … グリフの番号から、輪郭の点を読む関数（loca で glyf の中の位置を求めて、点を読む）
// Add the items for this episode here.
// read_file ... Read a whole file into a byte array.
// read_u16, read_u32, read_i16 ... Read 2-byte or 4-byte integers from a byte array at an offset.
// find_table ... Find a table offset by name (such as "head") in the table directory (static because it is only used in this file).
// load_font ... Read a font file into Font (table directory and offsets for head, maxp, cmap, loca, and glyf).
// find_glyph_index ... Look up a glyph ID from a character code in cmap.
// load_glyph ... Read outline points by glyph ID (find the location in glyf with loca, then read the points).
