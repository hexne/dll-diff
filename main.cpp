import std;
import dll_info;

int main() {
	constexpr std::string_view dll_path = R"(C:\Users\hexne\Desktop\PocoJSONd.dll)";
	constexpr std::string_view pdb_path = R"(C:\Users\hexne\Desktop\PocoJSONd.pdb)";

	auto interfaces = dll_info::parse_interface_filter(dll_path, "Poco");
	auto classes = dll_info::parse_class_filter(pdb_path, "Poco");
	auto enums = dll_info::parse_enum_filter(pdb_path, "Poco");

	std::println("\n=== interfaces ({}) ===", interfaces.size());
	for (auto& info : interfaces) {
		std::println("{}", info.remove_cvref_string());
	}

	std::println("\n=== classes ({}) ===", classes.size());
	for (auto& type : classes) {
		std::println("{}", type.remove_cvref_string());
	}
	std::println("=== enums ({}) ===", enums.size());
	for (auto& en : enums) {
		for (const auto& val : en.values) {
			std::println("{}", val.string());
		}
	}
}
