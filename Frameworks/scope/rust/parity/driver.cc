// Reads lines "selector\tleft\tright" from stdin and prints, per line, the
// selector's canonical form and the exact match rank. Built twice by run.sh:
// once against the original C++ scope sources, once against the Rust port.
#include <scope/scope.h>
#include <iostream>
#include <cstdio>

int main ()
{
	std::string line;
	while(std::getline(std::cin, line))
	{
		size_t a = line.find('\t'), b = line.find('\t', a + 1);
		if(a == std::string::npos || b == std::string::npos)
			continue;

		scope::selector_t sel(line.substr(0, a));
		scope::context_t ctx(scope::scope_t(line.substr(a + 1, b - a - 1)), scope::scope_t(line.substr(b + 1)));

		std::optional<double> rank = sel.does_match(ctx);
		char buf[64];
		if(rank)
			snprintf(buf, sizeof(buf), "%a", *rank);
		std::cout << to_s(sel) << "\t" << (rank ? buf : "-") << "\n";
	}
	return 0;
}
