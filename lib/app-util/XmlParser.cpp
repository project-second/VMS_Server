#include "XmlParser.h"
#include <functional>
#include <pugixml.hpp>

std::string get_tag_text(const std::string& xml, const std::string& tag_name_no_prefix) {
    pugi::xml_document doc;
    const pugi::xml_parse_result parsed = doc.load_buffer(xml.data(), xml.size());
    if (!parsed) {
        return {};
    }

    const auto local_name = [](const char* qname) -> std::string {
        if (qname == nullptr) return {};
        const std::string full_name{qname};
        const std::size_t colon_pos = full_name.find(':');
        if (colon_pos == std::string::npos) return full_name;
        return full_name.substr(colon_pos + 1);
    };

    const std::function<std::string(pugi::xml_node)> find_tag_text =
        [&](pugi::xml_node parent) -> std::string {
            for (pugi::xml_node node = parent.first_child(); node; node = node.next_sibling()) {
                if (node.type() != pugi::node_element) {
                    continue;
                }

                if (local_name(node.name()) == tag_name_no_prefix) {
                    std::string text = node.text().as_string();
                    trim_inplace(text);
                    return text;
                }

                std::string found = find_tag_text(node);
                if (!found.empty()) {
                    return found;
                }
            }
            return {};
        };

    return find_tag_text(doc);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && !std::isspace((unsigned char)s[j])) ++j;
        out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

// TODO 우선 우선순위가 http 가 먼저이지만, 나중에 https 우선으로 바꿔야함
std::string pick_best_device_service(const std::vector<std::string>& urls) {
    auto is_ipv6_bracket = [](const std::string& u){ return u.find("://[") != std::string::npos; };

    for (auto& u : urls) if (u.rfind("http://", 0) == 0 && !is_ipv6_bracket(u)) return u;
    for (auto& u : urls) if (u.rfind("https://", 0) == 0 && !is_ipv6_bracket(u)) return u;
    for (auto& u : urls) if (u.rfind("http://", 0) == 0) return u;
    for (auto& u : urls) if (u.rfind("https://", 0) == 0) return u;

    return urls.empty() ? std::string{} : urls.front();
}
