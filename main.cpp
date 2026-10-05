import std;
import dll_info;

int main() {
	const dll_info::com_guard com;

	constexpr std::string_view pdb_path = R"(C:\Users\hexne\Desktop\PocoJSONd.pdb)";
	auto dll_info = dll_info::parse_dll(R"(C:\Users\hexne\Desktop\PocoJSONd.dll)");
	auto pdb_info = dll_info::parse_pdb(pdb_path);
	auto enums = dll_info::parse_enums(pdb_path);

	// std::vector<Interface> 

	std::set<std::string> set;
	for (auto& info : dll_info) 
		set.insert(info.string());

	for (auto& info : pdb_info) {
		if (set.find(info.string()) == set.end())
			continue;

		std::println ("{}", info.remove_cvref_string());
	}
	//std::println("\n=== enums ({}) ===", enums.size());
	//for (auto& info : enums) {
	//	std::print("{}", info.namespace_name.empty() ? info.name : info.namespace_name + "::" + info.name);
	//	std::print(" ({} bytes): ", info.size);

	//	for (std::size_t i = 0; i < info.members.size(); ++i) {
	//		if (i != 0)
	//			std::print(", ");

	//		std::print("{}={}", info.members[i].name, info.members[i].value);
	//	}
	//	std::println();
	//}

}
