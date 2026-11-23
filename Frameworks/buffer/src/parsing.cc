#include "buffer.h"
#include "meta_data.h"

namespace ng
{
	// ===================
	// = buffer_parser_t =
	// ===================

	struct result_t
	{
		parse::stack_ptr state;
		std::map<size_t, scope::scope_t> scopes;
	};

	result_t handle_request (parse::grammar_ptr grammar, parse::stack_ptr state, std::string const& line, std::pair<size_t, size_t> range)
	{
		std::lock_guard<std::mutex> lock(grammar->mutex());

		result_t result;
		result.state = parse::parse(line.data(), line.data() + line.size(), state, result.scopes, range.first == 0);
		return result;
	}

	// ============
	// = buffer_t =
	// ============

	void buffer_t::initiate_repair (size_t limit_redraw, size_t batch_start)
	{
		if(!_async_parsing || _parser_running)
			return;

		if(!_dirty.empty() && !_parser_states.empty())
		{
			size_t n       = convert(_dirty.begin()->first).line;
			size_t from    = begin(n);
			size_t to      = end(n);
			auto stateIter = from == 0 ? _parser_states.begin() : _parser_states.find(from);
			if(stateIter != _parser_states.end())
			{
				if(batch_start == -1)
					batch_start = from;

				auto grammarRef = grammar();
				auto state      = stateIter->second;
				auto line       = substr(from, to);

				size_t bufferRev = revision();
				auto bufferRef   = parser_reference();
				_parser_running  = true;

				CFRunLoopRef runLoop = CFRunLoopGetCurrent();
				dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
					result_t result = handle_request(grammarRef, state, line, { from, to });
					CFRunLoopPerformBlock(runLoop, kCFRunLoopCommonModes, ^{
						if(bufferRef.lock())
						{
							_parser_running = false;
							if(bufferRev == revision())
							{
								update_scopes(limit_redraw, batch_start, { from, to }, result.scopes, result.state);
							}
							else
							{
								// We didn’t inform anyone about the previous lines parsed
								if(batch_start != from)
									did_parse(std::min(batch_start, size()), std::min(from, size()));
								initiate_repair();
							}
						}
					});
					CFRunLoopWakeUp(runLoop);
				});
			}
			else
			{
				os_log_error(OS_LOG_DEFAULT, "No parser state for %zu-%zu (%p)\n%{public}s\n%{public}s", from, to, this, substr(0, size()).c_str(), to_s(*this).c_str());
			}
		}
	}

	void buffer_t::update_scopes (size_t limit_redraw, size_t batch_start, std::pair<size_t, size_t> const& range, std::map<size_t, scope::scope_t> const& newScopes, parse::stack_ptr parserState)
	{
		bool atEOF = convert(range.first).line+1 == lines();
		_scopes.remove(_scopes.lower_bound(range.first), atEOF ? _scopes.end() : _scopes.lower_bound(range.second));
		for(auto const& pair : newScopes)
		{
			if(range.first + pair.first < range.second || atEOF)
				_scopes.set(range.first + pair.first, pair.second);
		}

		_dirty.remove(_dirty.lower_bound(range.first), atEOF ? _dirty.end() : _dirty.lower_bound(range.second));
		if((_parser_states.find(range.second) == _parser_states.end() || !parse::equal(parserState, (_parser_states.find(range.second)->second))))
		{
			_parser_states.set(range.second, parserState);
			if(!atEOF)
				_dirty.set(range.second, true);
		}

		_parser_reference.reset();
		_parser_running = false;

		bool next_line_needs_update = !_dirty.empty() && _dirty.begin()->first == range.second;
		if(!next_line_needs_update || limit_redraw == 0)
		{
			did_parse(batch_start, range.second);
			initiate_repair(10);
		}
		else
		{
			initiate_repair(limit_redraw == 0 ? 0 : limit_redraw-1, batch_start);
		}
	}

	void buffer_t::wait_for_repair ()
	{
		if(!grammar())
			return;

		_parser_reference.reset();
		_parser_running = false;

		// Using the NSSpellChecker API while main thread is blocked can result in this exception:
		// Dispatch Thread Soft Limit Reached: 64 (too many dispatch threads blocked in synchronous operations)
		// Therefor we disable spell checking during refresh. First noticed with macOS 10.12
		if(_spelling)
			_spelling->set_disabled(true);

		size_t repairedFrom = SIZE_T_MAX, repairedTo = 0;

		std::lock_guard<std::mutex> lock(grammar()->mutex());
		while(!_dirty.empty() && !_parser_states.empty())
		{
			size_t n    = convert(_dirty.begin()->first).line;
			size_t from = begin(n);
			size_t to   = end(n);
			repairedFrom = std::min(repairedFrom, from);
			repairedTo   = std::max(repairedTo, to);
			auto state  = from == 0 ? _parser_states.begin() : _parser_states.find(from);
			if(state == _parser_states.end())
			{
				os_log_error(OS_LOG_DEFAULT, "No parser state for %zu-%zu (%p)\n%{public}s", from, to, this, substr(0, size()).c_str());
				break;
			}

			std::string const line = substr(from, to);
			std::map<size_t, scope::scope_t> newScopes;
			auto newState = parse::parse(line.data(), line.data() + line.size(), state->second, newScopes, from == 0);
			update_scopes(0, from, std::make_pair(from, to), newScopes, newState);
		}

		if(_spelling)
		{
			_spelling->set_disabled(false);

			// The lines parsed above had their misspellings removed but were not
			// checked: do that when the run loop is free again, rather than leave
			// them unmarked until the text is next edited.
			if(repairedFrom < repairedTo)
			{
				bool const scheduled   = _spelling_recheck_from < _spelling_recheck_to;
				_spelling_recheck_from = std::min(_spelling_recheck_from, repairedFrom);
				_spelling_recheck_to   = std::max(_spelling_recheck_to, repairedTo);

				if(!scheduled)
					schedule_spelling_recheck();
			}
		}
	}

	// Spell checking runs at well under a megabyte per second, so the pending
	// range is checked a slice at a time, one slice per pass of the run loop,
	// rather than in one go that would freeze a large document for seconds.
	void buffer_t::schedule_spelling_recheck ()
	{
		static size_t const kSliceSize = 16 * 1024;

		if(!_spelling_recheck_reference)
			_spelling_recheck_reference = std::make_shared<bool>(true);
		std::weak_ptr<bool> bufferRef = _spelling_recheck_reference;

		CFRunLoopRef runLoop = CFRunLoopGetCurrent();
		CFRunLoopPerformBlock(runLoop, kCFRunLoopCommonModes, ^{
			if(!bufferRef.lock())
				return;

			size_t const from = std::min(_spelling_recheck_from, size());
			size_t const to   = std::min(_spelling_recheck_to, size());
			if(!_spelling || from >= to)
			{
				_spelling_recheck_from = SIZE_T_MAX;
				_spelling_recheck_to   = 0;
				return;
			}

			// whole lines, so that no word is split between two slices
			size_t const sliceEnd = std::min(to, end(convert(std::min(from + kSliceSize, to)).line));
			recheck_spelling(from, sliceEnd);

			if(sliceEnd < to)
			{
				_spelling_recheck_from = sliceEnd;
				schedule_spelling_recheck();
			}
			else
			{
				_spelling_recheck_from = SIZE_T_MAX;
				_spelling_recheck_to   = 0;
			}
		});
		CFRunLoopWakeUp(runLoop);
	}

} /* ng */
