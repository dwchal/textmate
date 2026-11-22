#ifndef LSP_POSITION_H
#define LSP_POSITION_H

#include <string>

// TextMate positions are (line, byte offset in line), UTF-8. LSP positions are
// (line, character) where “character” counts UTF-16 code units unless the
// server agreed to UTF-8. These convert a column within a single line.

namespace lsp
{
	inline size_t utf8_length_of_sequence (unsigned char lead)
	{
		if(lead < 0x80)           return 1;
		if((lead & 0xE0) == 0xC0) return 2;
		if((lead & 0xF0) == 0xE0) return 3;
		if((lead & 0xF8) == 0xF0) return 4;
		return 1; // malformed: count the byte on its own
	}

	// UTF-16 column → byte column. Columns past the end clamp to the line length.
	inline size_t utf16_to_utf8_column (std::string const& line, size_t utf16Column)
	{
		size_t bytes = 0, units = 0;
		while(bytes < line.size() && units < utf16Column)
		{
			size_t const len = utf8_length_of_sequence(line[bytes]);
			if(bytes + len > line.size())
				break;
			units += len == 4 ? 2 : 1;
			bytes += len;
		}
		return bytes;
	}

	// Byte column → UTF-16 column. A byte column inside a sequence counts the whole character.
	inline size_t utf8_to_utf16_column (std::string const& line, size_t byteColumn)
	{
		size_t bytes = 0, units = 0;
		while(bytes < line.size() && bytes < byteColumn)
		{
			size_t const len = utf8_length_of_sequence(line[bytes]);
			units += len == 4 ? 2 : 1;
			bytes += len;
		}
		return units;
	}

} /* lsp */

#endif
