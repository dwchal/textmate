#include "paragraph.h"
#include "ct.h"
#include "render.h"
#include <cf/cf.h>
#include <text/parse.h>
#include <text/utf8.h>
#include <regexp/format_string.h>

static double const kFoldingDotsRatio = 20.0 / 10.0; // FIXME Folding dots ratio should be obtained from the image and given to layout_t
static size_t const kCTLineMaxSize = 2048;

namespace ng
{
	namespace
	{
		static std::string representation_for (uint32_t ch)
		{
			static std::set<uint32_t> const SpaceCharacters = {
				0x2028, // LINE SEPARATOR
				0x2029, // PARAGRAPH SEPARATOR
				0x2060, // WORD JOINER
				0xFEFF  // ZERO WIDTH NO-BREAK SPACE
			};

			if(0x20 <= ch && ch <= 0x7E || ch == '\t' || ch == '\n')
				return NULL_STR;

			switch(ch)
			{
				case '\f': return "<NP>";
				case '\r': return "<CR>";
				case '\b': return "<BS>";
				case 0x00: return "<NUL>";
				case 0x1B: return "<ESC>";
				case 0x1C: return "<FS>";
				case 0x1D: return "<GS>";
				case 0x1E: return "<RS>";
				case 0x1F: return "<US>";
				case 0xA0: return "·";

				default:
				{
					if(0x00 < ch && ch <= 'Z'-'A'+1)
						return "^" + std::string(1, ch-1+'A');
					else if(ch < 0x20 || (0x7E < ch && ch < 0xA0))
						return "◆";
					else if(SpaceCharacters.find(ch) != SpaceCharacters.end())
						return text::format("<U+%04X>", ch);
				}
				break;
			}
			return NULL_STR;
		}

		static void draw_line (CGPoint pos, std::string const& text, CGColorRef color, CTFontRef font, CGContextRef context, bool isFlipped)
		{
			ASSERT(utf8::is_valid(text.begin(), text.end()));
			if(CFMutableAttributedStringRef str = CFAttributedStringCreateMutable(kCFAllocatorDefault, 0))
			{
				CFAttributedStringReplaceString(str, CFRangeMake(0, 0), cf::wrap(text));
				CFAttributedStringSetAttribute(str, CFRangeMake(0, CFAttributedStringGetLength(str)), kCTFontAttributeName, font);
				CFAttributedStringSetAttribute(str, CFRangeMake(0, CFAttributedStringGetLength(str)), kCTForegroundColorAttributeName, color);

				if(CTLineRef line = CTLineCreateWithAttributedString(str))
				{
					CGContextSaveGState(context);
					if(isFlipped)
						CGContextConcatCTM(context, CGAffineTransformMake(1, 0, 0, -1, 0, 2 * pos.y));
					CGContextSetTextPosition(context, pos.x, pos.y);
					CTLineDraw(line, context);
					CGContextRestoreGState(context);

					CFRelease(line);
				}
				CFRelease(str);
			}
		}
	}

	// =======================
	// = paragraph_t::node_t =
	// =======================

	void paragraph_t::node_t::insert (size_t i, size_t len)
	{
		_length += len;
		_line.reset();
	}

	void paragraph_t::node_t::erase (size_t from, size_t to)
	{
		ASSERT_LE(from, to); ASSERT_LE(to, _length);
		_length -= to - from;
		_line.reset();
	}

	void paragraph_t::node_t::did_update_scopes (size_t from, size_t to)
	{
		_line.reset();
	}

	void paragraph_t::node_t::layout (CGFloat x, size_t tabSize, theme_ptr const& theme, bool softWrap, size_t wrapColumn, ct::metrics_t const& metrics, ng::buffer_t const& buffer, size_t bufferOffset, std::string const& fillStr, markup_t const* markup)
	{
		if(_line)
			return;

		switch(_type)
		{
			case kNodeTypeText:
			{
				std::map<size_t, scope::scope_t> scopes = buffer.scopes(bufferOffset, bufferOffset + _length);
				if(markup)
					markup->apply(scopes, bufferOffset, bufferOffset + _length);
				_line = std::make_shared<ct::line_t>(buffer.substr(bufferOffset, bufferOffset + _length), scopes, theme, tabSize, metrics, nullptr);
			}
			break;

			case kNodeTypeUnprintable:
			{
				scope::scope_t scope = buffer.scope(bufferOffset).right;
				scope.push_scope("deco.unprintable");
				_line = std::make_shared<ct::line_t>(representation_for(utf8::to_ch(buffer.substr(bufferOffset, bufferOffset + _length))), std::map<size_t, scope::scope_t>{ { 0, scope } }, theme, tabSize, metrics, nullptr);
			}
			break;

			case kNodeTypeFolding:
			{
				_width = round(metrics.cap_height() * kFoldingDotsRatio);
			}
			break;

			case kNodeTypeConcealed:
			{
				_width = 0;
			}
			break;

			case kNodeTypeSoftBreak:
			{
				scope::context_t const context = buffer.scope(bufferOffset);
				scope::scope_t scope = shared_prefix(context.left, context.right);
				scope.push_scope("deco.indented-wrap");
				_line = std::make_shared<ct::line_t>(fillStr, std::map<size_t, scope::scope_t>{ { 0, scope } }, theme, tabSize, metrics, nullptr);
			}
			break;
		}
	}

	void paragraph_t::node_t::reset_font_metrics (ct::metrics_t const& metrics)
	{
		_line.reset();
	}

	CGFloat paragraph_t::node_t::width () const
	{
		return _line ? _line->width() : _width;
	}

	void paragraph_t::node_t::update_tab_width ()
	{
		_line.reset();
	}

	void paragraph_t::node_t::draw_background (theme_ptr const& theme, ng::context_t const& context, bool isFlipped, CGRect visibleRect, CGColorRef backgroundColor, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor, CGFloat lineHeight) const
	{
		if(_line)
			_line->draw_background(CGPointMake(anchor.x, anchor.y), lineHeight, context, isFlipped, backgroundColor);

		if(_type != kNodeTypeText && _type != kNodeTypeConcealed)
		{
			scope::scope_t scope = _type == kNodeTypeSoftBreak ? buffer.scope(bufferOffset).left : buffer.scope(bufferOffset).right;
			switch(_type)
			{
				case kNodeTypeUnprintable: scope.push_scope("deco.unprintable");   break;
				case kNodeTypeFolding:     scope.push_scope("deco.folding");       break;
				case kNodeTypeSoftBreak:   scope.push_scope("deco.indented-wrap"); break;
			}

			styles_t const styles = theme->styles_for_scope(scope);
			if(!CFEqual(backgroundColor, styles.background()))
			{
				CGFloat x1 = round(anchor.x);
				CGFloat x2 = round(_type == kNodeTypeSoftBreak || _type == kNodeTypeNewline ? CGRectGetMaxX(visibleRect) : anchor.x + _width);
				render::fill_rect(context, styles.background(), CGRectMake(x1, anchor.y, x2 - x1, lineHeight));
			}
		}
	}

	void paragraph_t::node_t::draw_foreground (theme_ptr const& theme, ng::context_t const& context, bool isFlipped, CGRect visibleRect, ng::buffer_t const& buffer, size_t bufferOffset, std::vector< std::pair<size_t, size_t> > const& misspelled, CGPoint anchor, CGFloat baseline) const
	{
		if(_line)
			_line->draw_foreground(CGPointMake(anchor.x, anchor.y + baseline), context, isFlipped, misspelled, theme);

		if(_type == kNodeTypeConcealed)
			return;

		if(_type != kNodeTypeNewline || context.newline().size())
		{
			std::string str = NULL_STR;
			scope::scope_t scope = buffer.scope(bufferOffset).right;
			switch(_type)
			{
				case kNodeTypeNewline:
					str = context.newline();
					scope.push_scope("deco.invisible.newline");
				break;
				case kNodeTypeFolding:
					scope.push_scope("deco.folding");
				break;
			}

			if(str != NULL_STR)
			{
				styles_t const styles = theme->styles_for_scope(scope);
				draw_line(CGPointMake(anchor.x, anchor.y + baseline), str, styles.foreground(), styles.font(), context, isFlipped);
			}

			if(_type == kNodeTypeFolding)
			{
				styles_t const styles = theme->styles_for_scope(scope);

				CGFloat x1 = round(anchor.x);
				CGFloat x2 = round(anchor.x + _width);
				CGFloat y2 = round(anchor.y + baseline);
				CGFloat y1 = y2 - round(_width / kFoldingDotsRatio);

				CGRect rect = CGRectMake(x1, y1, x2 - x1, y2 - y1);
				if(CGImageRef imageMask = context.folding_dots(CGRectGetWidth(rect), CGRectGetHeight(rect)))
				{
					CGContextSaveGState(context);
					CGContextClipToMask(context, rect, imageMask);
					render::fill_rect(context, styles.foreground(), rect);
					CGContextRestoreGState(context);
				}
			}
		}
	}

	// ===============
	// = paragraph_t =
	// ===============

	void paragraph_t::insert (size_t pos, size_t len, ng::buffer_t const& buffer, size_t bufferOffset)
	{
		std::vector<node_t> newNodes;

		std::string const str = buffer.substr(pos, pos + len);
		size_t from = 0, i = 0;
		citerate(ch, diacritics::make_range(str.data(), str.data() + str.size()))
		{
			if(*ch == '\n' || representation_for(*ch) != NULL_STR)
			{
				if(from != i)
					insert_text(pos - bufferOffset + from, i - from);

				if(*ch == '\n')
					insert_newline(pos - bufferOffset + i, ch.length());
				else
					insert_unprintable(pos - bufferOffset + i, ch.length());

				from = i + ch.length();
			}
			else if(i - from >= kCTLineMaxSize)
			{
				insert_text(pos - bufferOffset + from, i - from);
				from = i;
			}
			i += ch.length();
		}

		if(from != str.size())
			insert_text(pos - bufferOffset + from, str.size() - from);

		_dirty = true;
	}

	void paragraph_t::insert_folded (size_t pos, size_t len, ng::buffer_t const& buffer, size_t bufferOffset)
	{
		_nodes.insert(iterator_at(pos - bufferOffset), node_t(kNodeTypeFolding, len));
		_dirty = true;
	}

	void paragraph_t::insert_concealed (size_t pos, size_t len, ng::buffer_t const& buffer, size_t bufferOffset)
	{
		_nodes.insert(iterator_at(pos - bufferOffset), node_t(kNodeTypeConcealed, len));
		_dirty = true;
	}

	void paragraph_t::erase (size_t from, size_t to, ng::buffer_t const& buffer, size_t bufferOffset)
	{
		ASSERT_LE(bufferOffset, from); ASSERT_LE(to, bufferOffset + length());

		size_t i = bufferOffset;
		for(auto& node : _nodes)
		{
			size_t len = node.length();
			if(i <= from && from < i + len)
			{
				size_t last = std::min(to - i, len);
				node.erase(from - i, last);
				from = i + last;
				if(to - i <= last)
					break;
			}
			i += len;
		}

		for(auto it = _nodes.begin(); it != _nodes.end(); )
		{
			if(it->length() == 0 && it->type() != kNodeTypeSoftBreak)
					it = _nodes.erase(it);
			else	++it;
		}

		if(from != to)
			os_log_error(OS_LOG_DEFAULT, "Error erasing %zu-%zu, %zu", from, to, bufferOffset);

		_dirty = true;
	}

	void paragraph_t::did_update_scopes (size_t from, size_t to, ng::buffer_t const& buffer, size_t bufferOffset)
	{
		size_t i = bufferOffset;
		for(auto& node : _nodes)
		{
			node.did_update_scopes(from - i, to - i);
			i += node.length();
		}
		_dirty = true;
	}

	bool paragraph_t::layout (theme_ptr const& theme, bool softWrap, size_t wrapColumn, ct::metrics_t const& metrics, CGRect visibleRect, ng::buffer_t const& buffer, size_t bufferOffset, markup_t const* markup)
	{
		if(!_dirty)
			return false;

		std::vector<node_t> newNodes;
		bool hasFoldings = false;
		for(auto const& node : _nodes)
		{
			if(node.type() != kNodeTypeSoftBreak)
				newNodes.push_back(node);
			hasFoldings = hasFoldings || node.type() == kNodeTypeFolding;
		}
		_nodes.swap(newNodes);

		size_t const tabSize = buffer.indent().tab_size();
		std::string fillStr = NULL_STR;
		if(!hasFoldings && softWrap)
		{
			fillStr = "";
			size_t fillStrWidth = 0;

			std::string str = buffer.substr(bufferOffset, bufferOffset + length());
			ASSERT(utf8::is_valid(str.begin(), str.end()));

			// Hidden markers take no room: wrap the text that is actually shown,
			// then map the break positions back (‘hidden’ holds from → length).
			std::vector< std::pair<size_t, size_t> > hidden;
			{
				size_t offset = 0;
				for(auto const& node : _nodes)
				{
					if(node.type() == kNodeTypeConcealed)
						hidden.emplace_back(offset, node.length());
					offset += node.length();
				}
			}

			if(!hidden.empty())
			{
				std::string shown;
				size_t offset = 0;
				for(auto const& pair : hidden)
				{
					shown.append(str, offset, pair.first - offset);
					offset = pair.first + pair.second;
				}
				shown.append(str, offset, std::string::npos);
				str.swap(shown);
			}

			// Larger text fits fewer columns
			double const scale = markup ? markup->heading_scale(bufferOffset, bufferOffset + length()) : 1;
			if(scale > 1 && wrapColumn > 0)
				wrapColumn = std::max<size_t>(10, floor(wrapColumn / scale));

			bundles::item_ptr indentedSoftWrapItem;
			scope::context_t scope(buffer.scope(bufferOffset, false).right, buffer.scope(bufferOffset + length(), false).left);
			plist::any_t const& indentedSoftWrapValue = bundles::value_for_setting("indentedSoftWrap", scope, &indentedSoftWrapItem);
			if(indentedSoftWrapItem)
			{
				std::string pattern, format;
				if(plist::get_key_path(indentedSoftWrapValue, "match", pattern) && plist::get_key_path(indentedSoftWrapValue, "format", format))
				{
					if(regexp::match_t const& m = regexp::search(pattern, str))
					{
						std::string tmp = format_string::expand(format, m.captures());
						citerate(ch, diacritics::make_range(tmp.data(), tmp.data() + tmp.size()))
						{
							if(*ch == '\t')
									fillStr.append(std::string(tabSize - (fillStrWidth % tabSize), ' '));
							else	fillStr.append(&ch, ch.length());
							fillStrWidth += (*ch == '\t' ? tabSize - (fillStrWidth % tabSize) : 1);
						}
					}
				}

				if(wrapColumn < fillStrWidth)
				{
					fillStr = "    ";
					fillStrWidth = 4;
				}
			}

			for(size_t offset : text::soft_breaks(str, wrapColumn, tabSize, fillStrWidth))
			{
				for(auto const& pair : hidden)
				{
					if(pair.first < offset)
						offset += pair.second;
				}
				_nodes.insert(iterator_at(offset), node_t(kNodeTypeSoftBreak, 0, fillStrWidth * metrics.column_width()));
			}
		}

		CGFloat x = 0;
		size_t i = bufferOffset;
		for(auto& node : _nodes)
		{
			node.layout(x, tabSize, theme, softWrap, wrapColumn, metrics, buffer, i, fillStr, markup);
			x += node.width();
			i += node.length();
		}

		_dirty = false;
		return true;
	}

	std::vector<paragraph_t::softline_t> paragraph_t::softlines (ct::metrics_t const& metrics, bool softBreaksOnNewline) const
	{
		std::vector<softline_t> softlines;

		CGFloat x = 0, y = 0;
		size_t first = 0, firstOffset = 0, offset = 0;
		CGFloat ascent = 0, descent = 0, leading = 0;
		for(size_t i = 0; i < _nodes.size(); ++i)
		{
			auto node = _nodes.begin() + i;
			if(node->type() == kNodeTypeSoftBreak)
			{
				softlines.emplace_back(firstOffset, x, y, metrics.baseline(ascent), metrics.line_height(ascent, descent, leading), first, i + (softBreaksOnNewline ? 0 : 1));
				firstOffset = offset;
				x = (softBreaksOnNewline ? 0 : node->width());
				y += metrics.line_height(ascent, descent, leading);
				first = i + (softBreaksOnNewline ? 0 : 1);
				ascent = 0;
				descent = 0;
				leading = 0;
			}

			if(node->line())
			{
				CGFloat a, d, l;
				node->line()->width(&a, &d, &l);
				ascent  = std::max(ascent,  a);
				descent = std::max(descent, d);
				leading = std::max(leading, l);
			}

			offset += node->length();
		}

		softlines.emplace_back(firstOffset, x, y, metrics.baseline(ascent), metrics.line_height(ascent, descent, leading), first, _nodes.size());
		return softlines;
	}

	size_t paragraph_t::softline_count (ct::metrics_t const& metrics, bool softBreaksOnNewline) const
	{
		return softlines(metrics, softBreaksOnNewline).size();
	}

	size_t paragraph_t::softline_for_index (ng::index_t const& index, ng::buffer_t const& buffer, size_t bufferOffset, size_t softlineOffset, ct::metrics_t const& metrics, bool softBreaksOnNewline) const
	{
		std::vector<softline_t> const softlines = this->softlines(metrics, softBreaksOnNewline);
		auto it = std::upper_bound(softlines.begin(), softlines.end(), index.index - bufferOffset, [](size_t offset, softline_t const& softline) -> bool {
			return offset < softline.offset;
		});
		ASSERT(it != softlines.begin());
		return softlineOffset + (--it - softlines.begin());
	}

	ng::range_t paragraph_t::range_for_softline (size_t softline, ng::buffer_t const& buffer, size_t bufferOffset, size_t softlineOffset, ct::metrics_t const& metrics, bool softBreaksOnNewline) const
	{
		std::vector<softline_t> const softlines = this->softlines(metrics, softBreaksOnNewline);
		ASSERT_LT(softline - softlineOffset, softlines.size());
		auto it = softlines.begin() + (softline - softlineOffset);
		ng::range_t range(it->offset);
		range.last = ++it != softlines.end() ? it->offset : length();
		return range + bufferOffset;
	}

	std::vector<paragraph_t::node_t>::iterator paragraph_t::iterator_at (size_t i)
	{
		size_t from = 0;
		iterate(node, _nodes)
		{
			if(from == i)
				return node;
			else if(from < i && i < from + node->length())
			{
				node_type_t const type = node->type();
				size_t len = node->length() - (i - from);
				node->erase(i - from, node->length());
				return _nodes.insert(++node, node_t(type, len));
			}
			from += node->length();
		}
		return _nodes.end();
	}

	void paragraph_t::insert_text (size_t i, size_t len)
	{
		size_t from = 0;
		for(auto& node : _nodes)
		{
			if(from <= i && i <= from + node.length() && node.type() == kNodeTypeText)
			{
				if(i == from + node.length() && node.length() >= kCTLineMaxSize)
					break;
				return node.insert(i - from, len);
			}
			from += node.length();
		}
		_nodes.insert(iterator_at(i), node_t(kNodeTypeText, len));
	}

	void paragraph_t::insert_unprintable (size_t i, size_t len)
	{
		_nodes.insert(iterator_at(i), node_t(kNodeTypeUnprintable, len));
	}

	void paragraph_t::insert_newline (size_t i, size_t len)
	{
		_nodes.insert(iterator_at(i), node_t(kNodeTypeNewline, len));
	}

	void paragraph_t::set_wrapping (bool softWrap, size_t wrapColumn, ct::metrics_t const& metrics)
	{
		_dirty = true;
	}

	void paragraph_t::set_tab_size (ct::metrics_t const& metrics)
	{
		_dirty = true;

		auto lines = softlines(metrics);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
				node->update_tab_width();
		}
	}

	void paragraph_t::reset_font_metrics (ct::metrics_t const& metrics)
	{
		_dirty = true;
		for(auto& node : _nodes)
			node.reset_font_metrics(metrics);
	}

	size_t paragraph_t::length () const
	{
		size_t res = 0;
		for(auto const& node : _nodes)
			res += node.length();
		return res;
	}

	CGFloat paragraph_t::width () const
	{
		CGFloat x = 0, res = 0;
		for(auto const& node : _nodes)
		{
			if(node.type() == kNodeTypeSoftBreak)
				x = 0;
			x += node.width();
			res = std::max(x, res);
		}
		return res;
	}

	CGFloat paragraph_t::height (ct::metrics_t const& metrics) const
	{
		auto lines = softlines(metrics);
		return lines.back().y + lines.back().height;
	}

	ng::index_t paragraph_t::index_at_point (CGPoint point, ct::metrics_t const& metrics, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor) const
	{
		auto lines = softlines(metrics);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			if(anchor.y + lines[i].y <= point.y && point.y < anchor.y + lines[i].y + lines[i].height)
			{
				CGFloat x = lines[i].x;
				size_t offset = lines[i].offset;
				foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
				{
					if(node->type() == kNodeTypeSoftBreak)
					{
						size_t res = bufferOffset + offset;
						if(i+1 != lines.size() && res == bufferOffset + lines[i+1].offset)
							res -= buffer[res-1].size();
						return res;
					}
					else if(node->type() == kNodeTypeNewline)
					{
						size_t carry = point.x > anchor.x + x ? (size_t)floor((point.x - (anchor.x + x)) / metrics.column_width()) : 0;
						return ng::index_t(bufferOffset + offset, carry);
					}

					if(point.x <= anchor.x + x)
						return bufferOffset + offset;
					else if(anchor.x + x < point.x && point.x < anchor.x + x + node->width())
					{
						CGFloat delta = point.x - (anchor.x + x);
						if(node->type() == kNodeTypeText && node->line())
						{
							size_t res = bufferOffset + offset + node->line()->index_for_offset(delta);
							if(i+1 != lines.size() && res == bufferOffset + lines[i+1].offset)
								res -= buffer[res-1].size();
							return res;
						}
						return bufferOffset + offset + lround(delta / node->width()) * node->length();
					}

					x += node->width();
					offset += node->length();
				}
			}
		}
		return bufferOffset + length();
	}

	CGRect paragraph_t::rect_at_index (ng::index_t const& index, ct::metrics_t const& metrics, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor, bool bol_as_eol, bool wantsBaseline) const
	{
		size_t needle = index.index - bufferOffset;
		CGFloat caretOffset = index.carry * metrics.column_width();

		auto lines = softlines(metrics);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			if(lines[i].offset <= needle && (i+1 == lines.size() || needle < lines[i+1].offset || (bol_as_eol && needle == lines[i+1].offset)))
			{
				CGFloat x = lines[i].x, y = lines[i].y;
				CGFloat height = wantsBaseline ? lines[i].baseline : lines[i].height;
				size_t offset = lines[i].offset;
				foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
				{
					if(offset <= needle && needle < offset + node->length())
					{
						if(node->type() == kNodeTypeText && node->line())
							return CGRectMake(anchor.x + x + node->line()->offset_for_index(needle - offset) + caretOffset, anchor.y + y, metrics.column_width(), height);
						return CGRectMake(anchor.x + x + (needle - offset) * node->width() / node->length() + caretOffset, anchor.y + y, 1, height);
					}

					x += node->width();
					offset += node->length();
				}
				return CGRectMake(anchor.x + x + caretOffset, anchor.y + y, 1, height);
			}
		}

		CGFloat height = wantsBaseline ? lines.back().baseline : lines.back().height;
		return CGRectMake(anchor.x + caretOffset, anchor.y, 1, height);
	}

	ng::line_record_t paragraph_t::line_record_for (size_t line, size_t pos, ct::metrics_t const& metrics, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor) const
	{
		size_t needle = pos - bufferOffset;

		auto lines = softlines(metrics);
		CGFloat y = anchor.y;
		for(size_t i = 0; i < lines.size(); ++i)
		{
			if(lines[i].offset <= needle && (i+1 == lines.size() || needle < lines[i+1].offset))
				return ng::line_record_t(line, lines[i].offset, y, y + lines[i].height, lines[i].baseline);
			y += lines[i].height;
		}
		return ng::line_record_t(line, 0, 0, 0, 0);
	}

	ng::range_t paragraph_t::folded_range_at_point (CGPoint point, ct::metrics_t const& metrics, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor) const
	{
		auto lines = softlines(metrics);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			if(anchor.y + lines[i].y <= point.y && point.y < anchor.y + lines[i].y + lines[i].height)
			{
				CGFloat x = lines[i].x;
				size_t offset = lines[i].offset;

				foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
				{
					if(point.x <= anchor.x + x)
						return {};
					else if(anchor.x + x < point.x && point.x < anchor.x + x + node->width() && node->type() == kNodeTypeFolding)
						return ng::range_t{ bufferOffset + offset, bufferOffset + offset + node->length() };

					x += node->width();
					offset += node->length();
				}
			}
		}
		return {};
	}

	size_t paragraph_t::bol (size_t index, ng::buffer_t const& buffer, size_t bufferOffset) const
	{
		size_t i = bufferOffset;
		size_t bol = i;
		for(auto const& node : _nodes)
		{
			if(index < i)
				break;
			i += node.length();
			if(node.type() == kNodeTypeSoftBreak)
				bol = i;
		}
		return bol;
	}

	size_t paragraph_t::eol (size_t index, ng::buffer_t const& buffer, size_t bufferOffset) const
	{
		size_t i = bufferOffset;
		for(auto const& node : _nodes)
		{
			if(index < i && node.type() == kNodeTypeSoftBreak)
				return i - buffer[i-1].size();
			if(index <= i && node.type() == kNodeTypeNewline)
				return i;
			i += node.length();
		}
		return i;
	}

	size_t paragraph_t::index_left_of (size_t index, ng::buffer_t const& buffer, size_t bufferOffset) const
	{
		if(index != bufferOffset)
			index -= buffer[index-1].size();

		size_t i = bufferOffset;
		for(auto const& node : _nodes)
		{
			if(i < index && index < i + node.length() && (node.type() == kNodeTypeFolding || node.type() == kNodeTypeConcealed))
				return i;
			i += node.length();
		}

		return index;
	}

	size_t paragraph_t::index_right_of (size_t index, ng::buffer_t const& buffer, size_t bufferOffset) const
	{
		if(index != bufferOffset + length())
			index += buffer[index].size();

		size_t i = bufferOffset;
		for(auto const& node : _nodes)
		{
			if(i < index && index < i + node.length() && (node.type() == kNodeTypeFolding || node.type() == kNodeTypeConcealed))
				return i + node.length();
			i += node.length();
		}

		return index;
	}

	void paragraph_t::draw_background (theme_ptr const& theme, ct::metrics_t const& metrics, ng::context_t const& context, bool isFlipped, CGRect visibleRect, CGColorRef backgroundColor, ng::buffer_t const& buffer, size_t bufferOffset, CGPoint anchor) const
	{
		auto lines = softlines(metrics);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			CGFloat x = lines[i].x;
			size_t offset = lines[i].offset;
			foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
			{
				node->draw_background(theme, context, isFlipped, visibleRect, backgroundColor, buffer, bufferOffset + offset, CGPointMake(anchor.x + x, anchor.y + lines[i].y), lines[i].height);
				x += node->width();
				offset += node->length();
			}
		}
	}

	void paragraph_t::draw_foreground (theme_ptr const& theme, ct::metrics_t const& metrics, ng::context_t const& context, bool isFlipped, CGRect visibleRect, ng::buffer_t const& buffer, size_t bufferOffset, ng::ranges_t const& selection, CGPoint anchor) const
	{
		CGContextSetTextMatrix(context, CGAffineTransformMake(1, 0, 0, 1, 0, 0));

		auto lines = softlines(metrics, false);
		for(size_t i = 0; i < lines.size(); ++i)
		{
			CGFloat x = lines[i].x;
			size_t offset = bufferOffset + lines[i].offset;
			foreach(node, _nodes.begin() + lines[i].first, _nodes.begin() + lines[i].last)
			{
				std::vector< std::pair<size_t, size_t> > misspelled;
				if(node->type() == kNodeTypeText)
				{
					auto misspellings = buffer.misspellings(offset, offset + node->length());
					for(auto it = misspellings.begin(); it != misspellings.end(); )
					{
						bool flag = it->second;
						size_t from = it->first;
						size_t to = ++it != misspellings.end() ? it->first : node->length();
						if(flag)
						{
							bool intersects = false;
							for(auto const& range : selection)
								intersects = intersects || !(to + offset < range.min().index || range.max().index < from + offset);

							if(!intersects)
								misspelled.emplace_back(from, to);
						}
					}
				}

				node->draw_foreground(theme, context, isFlipped, visibleRect, buffer, offset, misspelled, CGPointMake(anchor.x + x, anchor.y + lines[i].y), lines[i].baseline);
				x += node->width();
				offset += node->length();
			}
		}
	}

	// ============
	// = markup_t =
	// ============

	static char const* markup_scope (unsigned kind)
	{
		switch(kind)
		{
			case 1:  return "markup.formatted.heading.1";
			case 2:  return "markup.formatted.heading.2";
			case 3:  return "markup.formatted.heading.3";
			case 4:  return "markup.formatted.heading.4";
			case 5:  return "markup.formatted.heading.5";
			case 6:  return "markup.formatted.heading.6";
			case 7:  return "markup.formatted.bold";
			case 8:  return "markup.formatted.italic";
			case 9:  return "markup.formatted.strikethrough";
			case 10: return "markup.formatted.code";
			case 11: return "markup.formatted.link";
		}
		return nullptr;
	}

	static std::vector<markup_t::span_t>::const_iterator first_style_near (markup_t const& markup, size_t from)
	{
		size_t const start = from > markup.longest_style ? from - markup.longest_style : 0;
		return std::lower_bound(markup.styles.begin(), markup.styles.end(), start, [](markup_t::span_t const& span, size_t offset){ return span.from < offset; });
	}

	void markup_t::apply (std::map<size_t, scope::scope_t>& scopes, size_t from, size_t to) const
	{
		if(styles.empty() || scopes.empty() || from >= to)
			return;

		std::vector<span_t const*> covering;
		for(auto it = first_style_near(*this, from); it != styles.end() && it->from < to; ++it)
		{
			if(from < it->to && markup_scope(it->kind))
				covering.push_back(&*it);
		}

		if(covering.empty())
			return;

		std::set<size_t> cuts;
		for(auto const& pair : scopes)
			cuts.insert(pair.first);
		for(auto span : covering)
		{
			cuts.insert(std::max(span->from, from) - from);
			if(span->to < to)
				cuts.insert(span->to - from);
		}

		std::map<size_t, scope::scope_t> res;
		for(size_t cut : cuts)
		{
			auto base = scopes.upper_bound(cut);
			if(base == scopes.begin())
				continue;

			scope::scope_t scope = (--base)->second;
			for(auto span : covering)
			{
				if(span->from <= from + cut && from + cut < span->to)
					scope.push_scope(markup_scope(span->kind));
			}
			res.emplace(cut, scope);
		}
		scopes.swap(res);
	}

	double markup_t::heading_scale (size_t from, size_t to) const
	{
		static double const scales[] = { 1, 1.6, 1.4, 1.2, 1.1, 1, 1 };
		double res = 1;
		for(auto it = first_style_near(*this, from); it != styles.end() && it->from < to; ++it)
		{
			if(from < it->to && 1 <= it->kind && it->kind <= 6)
				res = std::max(res, scales[it->kind]);
		}
		return res;
	}

	// ========
	// = to_s =
	// ========

	std::string to_s (paragraph_t const& paragraph)
	{
		return "paragraph";
	}

} /* ng */
