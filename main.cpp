import std;
import dll_info;

std::string format(dll_info::TypeInfo info) {
	std::string result;
	if (info.is_const) {
		result += "const ";
	}
	if (!info.namespace_name.empty()) {
		result += info.namespace_name + "::";
	}
	if (!info.name.empty()) {
		result += info.name;
	}
	else {
		result += "<unknown>"; 
	}
	if (info.is_ref) {
		if (info.is_left_ref)
			result += " &";
		else if (info.is_right_ref)
			result += " &&";
	}
	if (info.is_pointer)
		result += "*";
	if (info.is_top_const)
		result += " const";
	return result;
}

std::string format(dll_info::InterfaceInfo info) {
	std::string result;
	if (!info.namespace_name.empty()) {
		result += info.namespace_name + "::";
	}
	if (!info.class_name.empty()) {
		result += info.class_name + "::";
	}
	if (!info.name.empty()) {
		result += info.name;
	}
	return result;
}


int main() {
	const dll_info::com_guard com;

	constexpr std::string_view pdb_path = R"(C:\Users\hexne\Desktop\PocoJSONd.pdb)";
	auto dll_info = dll_info::parse_dll(R"(C:\Users\hexne\Desktop\PocoJSONd.dll)");
	auto pdb_info = dll_info::parse_pdb(pdb_path);
	auto enums = dll_info::parse_enums(pdb_path);

	std::set<std::string> set;
	for (auto& info : dll_info) 
		set.insert(format(info));

	for (auto& info : pdb_info) {
		if (set.find(format(info)) == set.end())
			continue;

		std::print("{}(", format(info));
		auto &args = info.args_info;
		for (std::size_t i = 0; i < args.size(); ++i) {
			if (i != 0)
				std::print(", ");

			std::print("{}", ::format(*args[i]));
		}
		std::print(")");
		if (info.is_const)
			std::print(" const");

		std::println();
	}

	std::println("\n=== enums ({}) ===", enums.size());
	for (auto& info : enums) {
		std::print("{}", info.namespace_name.empty() ? info.name : info.namespace_name + "::" + info.name);
		std::print(" ({} bytes): ", info.size);

		for (std::size_t i = 0; i < info.members.size(); ++i) {
			if (i != 0)
				std::print(", ");

			std::print("{}={}", info.members[i].name, info.members[i].value);
		}
		std::println();
	}

}
