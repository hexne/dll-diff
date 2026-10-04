export module filter;
import std;
import utils;

export namespace filter {
	export std::map<std::string, std::string> filters {
		{"NXOpen",   "PWOpen"},
		{"NXString", "PWString"},
	};
}
