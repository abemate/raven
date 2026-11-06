#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace raven {

struct TextMetrics {
	int32_t height,ascent,descent,internal_leading,external_leading,average_width,maximum_width;
	uint16_t first,last,default_character,break_character;
	uint8_t pitch;
};

struct TextExtent {
	int32_t width,height;
};

// Immutable typefaces and transient glyph masks; no Windows resource identities.
class TextRenderer {
public:
	explicit TextRenderer(const std::string &directory);
	~TextRenderer();
	TextRenderer(const TextRenderer &) = delete;
	TextRenderer &operator=(const TextRenderer &) = delete;
	TextMetrics metrics(std::span<const uint8_t,92> definition);
	TextExtent extent(std::span<const uint8_t,92> definition,std::span<const uint16_t> text);
	std::vector<int32_t> advances(std::span<const uint8_t,92> definition,std::span<const uint16_t> text);
	std::vector<uint8_t> mask(std::span<const uint8_t,92> definition,std::span<const uint16_t> text,
		uint32_t width,uint32_t height,int32_t x,int32_t y,std::span<const std::array<int32_t,2>> positions={});
	// The same text, placed as the mask places it, as anti-aliased coverage (0..255): the frame and menus draw their text with it.
	std::vector<uint8_t> smooth(std::span<const uint8_t,92> definition,std::span<const uint16_t> text,
		uint32_t width,uint32_t height,int32_t x,int32_t y);

	// Windows-shaped services for any logical font and any text: unknown faces fall back to the closest bundled family,
	// missing glyphs draw the face's replacement glyph, and rendering returns anti-aliased coverage.
	TextMetrics logical_metrics(std::span<const uint8_t,92> definition);
	// One table of the face a logical font uses, as GetFontData hands it out (tag in the file's byte order, 'OS/2' is 0x4f532f32);
	// empty when the face has no such table.
	std::vector<uint8_t> logical_table(std::span<const uint8_t,92> definition,uint32_t tag);
	// The whole font file of that face, as GetFontData hands out table 0, and whether it is a TrueType collection ('ttcf'), which
	// GetFontData hands out under that tag instead; empty when the face has no file.
	std::vector<uint8_t> logical_file(std::span<const uint8_t,92> definition,bool &collection);
	// Glyph indices as Windows names them: the glyphs of the one face a logical font uses, as its font file numbers them, so the
	// indices a program reads from the file GetFontData hands out and the ones GetGlyphIndices answers agree. A character the face has
	// no glyph for is glyph 0, or 0xffff when marked. The procedural symbol faces name their glyphs by character.
	std::vector<uint16_t> glyph_indices(std::span<const uint8_t,92> definition,std::span<const uint16_t> text,bool mark_missing);
	// Cumulative pixel advance after each glyph index of that face.
	std::vector<int32_t> glyph_advances(std::span<const uint8_t,92> definition,std::span<const uint16_t> glyphs);
	// The glyphs drawn as render draws text: coverage of the box, the first glyph at origin, offsets placing each one when given.
	std::vector<uint8_t> render_glyphs(std::span<const uint8_t,92> definition,std::span<const uint16_t> glyphs,
		uint32_t width,uint32_t height,int32_t origin_x,int32_t origin_y,std::span<const int32_t> offsets={});
	// The design units per em of that face, from its head table; 0 for the symbol face that has none.
	uint32_t logical_units_per_em(std::span<const uint8_t,92> definition);
	// Cumulative pixel advance after each UTF-16 unit; control characters advance by zero.
	std::vector<int32_t> logical_advances(std::span<const uint8_t,92> definition,std::span<const uint16_t> text);
	// Alpha coverage (0..255) of a box [x,x+width) x [y,y+height) in which text starts at left=origin_x, top=origin_y;
	// offsets, when given, are the x position of each unit relative to origin_x.
	std::vector<uint8_t> render(std::span<const uint8_t,92> definition,std::span<const uint16_t> text,
		uint32_t width,uint32_t height,int32_t origin_x,int32_t origin_y,std::span<const int32_t> offsets={});

private:
	struct State;
	std::unique_ptr<State> state;
};

}
