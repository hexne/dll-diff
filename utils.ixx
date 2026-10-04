export module utils;
import std;

export namespace utils {
    template<typename ...Ts>
    std::runtime_error
        format_runtime_error(std::format_string<Ts...>&& fmt, Ts&&... vs) {
        return std::runtime_error{ std::format<Ts...>(fmt, std::forward<Ts>(vs)...) };
    }

    // Replaces every occurrence of every key in `filters` with its value in a
    // single left-to-right pass: text inserted by a replacement is never
    // rescanned, so a value may safely contain another key's text.
    //
    // If two keys match at the same position (nested prefixes that both occur
    // in `str`) the table is ambiguous for this input and the call throws
    // instead of silently picking one. Empty keys are rejected upfront. Keys
    // and values generally differ in length, so the result is rebuilt rather
    // than patched in place.
    std::string replace(std::string str, const std::map<std::string, std::string>& filters) {
        for (const auto& entry : filters) {
            if (entry.first.empty()) {
                throw format_runtime_error("replace: filters contains an empty key");
            }
        }

        std::string result;
        result.reserve(str.size());

        std::size_t position = 0;
        while (position < str.size()) {
            const std::map<std::string, std::string>::value_type* match = nullptr;

            for (const auto& entry : filters) {
                if (str.compare(position, entry.first.size(), entry.first) == 0) {
                    if (match != nullptr) {
                        throw format_runtime_error(
                            "replace: keys '{}' and '{}' both match at position {} of '{}'",
                            match->first,
                            entry.first,
                            position,
                            str);
                    }

                    match = &entry;
                }
            }

            if (match != nullptr) {
                result += match->second;
                position += match->first.size();
            }
            else {
                result += str[position];
                ++position;
            }
        }

        return result;
    }
}
