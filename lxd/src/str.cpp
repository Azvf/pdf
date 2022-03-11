#include "str.h"
#include <string.h>
#include <assert.h>
#include <algorithm>
#include <cctype>

namespace lxd {
	void Upper(std::string& str) {
		std::transform(str.begin(), str.end(), str.begin(), std::toupper);
	}
	void Upper(std::wstring& str) {
		std::transform(str.begin(), str.end(), str.begin(), std::toupper);
	}
	void Lower(std::string& str) {
		std::transform(str.begin(), str.end(), str.begin(), std::tolower);
	}
	void Lower(std::wstring& str) {
		std::transform(str.begin(), str.end(), str.begin(), std::tolower);
	}

	std::string Lower(std::string_view str) {
		std::string result(str);
		std::transform(result.begin(), result.end(), result.begin(), std::tolower);
		return result;
	}

	std::vector<std::string_view> Split(std::string_view str, std::string_view delims) {
		std::vector<std::string_view> output;
		//output.reserve(str.size() / 2);

		for (auto first = str.data(), second = str.data(), last = first + str.size(); second != last && first != last; first = second + 1) {
			second = std::find_first_of(first, last, std::cbegin(delims), std::cend(delims));

			if (first != second)
				output.emplace_back(first, second - first);
		}

		return output;
	}
}