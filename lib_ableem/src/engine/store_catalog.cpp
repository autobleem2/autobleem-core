#include "ableem/engine/store_catalog.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

#include <json.h>

#include "ableem/engine/strings.h"

using namespace std;
using namespace nlohmann;

namespace ableem {

namespace {

string str(const json &item, const char *key) {
    auto it = item.find(key);
    return it != item.end() && it->is_string() ? it->get<string>() : string();
}

uint64_t num(const json &item, const char *key) {
    auto it = item.find(key);
    return it != item.end() && it->is_number_unsigned() ? it->get<uint64_t>()
           : it != item.end() && it->is_number_integer() && it->get<int64_t>() > 0
               ? static_cast<uint64_t>(it->get<int64_t>())
               : 0;
}

bool isUrl(const string &url) {
    return url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
}

string lower(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return s;
}

bool readAll(const string &path, string &text) {
    ifstream in(path, ios::binary);
    if (!in)
        return false;
    ostringstream ss;
    ss << in.rdbuf();
    text = ss.str();
    return true;
}

} // namespace

//*******************************
// StoreItem::size
//*******************************
uint64_t StoreItem::size() const {
    uint64_t total = 0;
    for (const StoreFile &f : files) {
        if (f.size == 0)
            return 0;
        total += f.size;
    }
    return total;
}

//*******************************
// StoreCatalog::loadJson / load
//*******************************
bool StoreCatalog::loadJson(const string &text, const string &sourceName, string &error) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        error = "not a store catalog (no JSON object)";
        return false;
    }
    schema = static_cast<int>(num(root, "schema"));
    platform = str(root, "platform");
    date = str(root, "date");
    items.clear();
    skipped = 0;
    auto list = root.find("items");
    if (list == root.end() || !list->is_array()) {
        error = "a store catalog without items";
        return false;
    }
    for (const json &j : *list) {
        if (!j.is_object()) {
            skipped++;
            continue;
        }
        StoreItem item;
        item.id = str(j, "id");
        item.kind = str(j, "kind");
        item.title = str(j, "title");
        item.version = str(j, "version");
        item.author = str(j, "author");
        item.licence = str(j, "licence");
        item.description = str(j, "description");
        item.serial = str(j, "serial");
        item.image = str(j, "image");
        item.source = sourceName;
        auto files = j.find("files");
        if (files != j.end() && files->is_array()) {
            for (const json &f : *files) {
                StoreFile file;
                file.name = str(f, "name");
                file.url = str(f, "url");
                file.size = num(f, "size");
                file.sha256 = lower(str(f, "sha256"));
                file.disc = static_cast<int>(num(f, "disc"));
                if (isUrl(file.url))
                    item.files.push_back(file);
            }
        }
        auto dependsOn = j.find("requires");
        if (dependsOn != j.end() && dependsOn->is_array())
            for (const json &r : *dependsOn)
                if (r.is_string())
                    item.dependsOn.push_back(r.get<string>());
        if (item.id.empty() || item.kind.empty() || item.title.empty() || item.files.empty()) {
            skipped++;
            continue;
        }
        stable_sort(item.files.begin(), item.files.end(),
                    [](const StoreFile &a, const StoreFile &b) { return a.disc < b.disc; });
        items.push_back(item);
    }
    return true;
}

bool StoreCatalog::load(const string &path, const string &sourceName, string &error) {
    string text;
    if (!readAll(path, text)) {
        error = "cannot read " + path;
        return false;
    }
    return loadJson(text, sourceName, error);
}

//*******************************
// StoreSourceTsv::parse
//*******************************
StoreSourceTsv StoreSourceTsv::parse(const string &text, const string &fallbackName) {
    StoreSourceTsv out;
    out.name = fallbackName;
    vector<string> columns; // empty = no header: title, url, size
    map<string, size_t> byKey; // kind + "\t" + title -> index in items
    istringstream in(text);
    string line;
    int number = 0;
    while (getline(in, line)) {
        number++;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (number == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0)
            line.erase(0, 3); // a BOM an editor left
        if (Strings::trim(line).empty())
            continue;
        if (line[0] == '#') {
            string comment = Strings::trim(line.substr(1));
            if (lower(comment.substr(0, 5)) == "name:")
                out.name = Strings::trim(comment.substr(5));
            continue;
        }
        vector<string> fields;
        {
            size_t start = 0;
            for (;;) {
                size_t tab = line.find('\t', start);
                fields.push_back(Strings::trim(line.substr(start, tab == string::npos ? string::npos : tab - start)));
                if (tab == string::npos)
                    break;
                start = tab + 1;
            }
        }
        // the header: the first line with a "url" field, before any data
        if (columns.empty() && out.items.empty()) {
            bool header = false;
            for (const string &f : fields)
                header = header || lower(f) == "url";
            if (header) {
                for (const string &f : fields)
                    columns.push_back(lower(f));
                continue;
            }
        }
        auto field = [&](const string &name) -> string {
            if (columns.empty()) {
                size_t index = name == "title" ? 0 : name == "url" ? 1 : name == "size" ? 2 : string::npos;
                return index < fields.size() ? fields[index] : string();
            }
            for (size_t i = 0; i < columns.size() && i < fields.size(); i++)
                if (columns[i] == name)
                    return fields[i];
            return string();
        };

        const string title = field("title");
        const string url = field("url");
        if (title.empty()) {
            out.problems.push_back("line " + to_string(number) + ": no title");
            continue;
        }
        if (!isUrl(url)) {
            out.problems.push_back("line " + to_string(number) + ": no http(s) url");
            continue;
        }
        string kind = lower(field("kind"));
        if (kind.empty())
            kind = "ps1";
        StoreFile file;
        file.url = url;
        file.name = field("name");
        const string size = field("size");
        if (!size.empty()) {
            char *end = nullptr;
            unsigned long long value = strtoull(size.c_str(), &end, 10);
            if (end != nullptr && *end == 0)
                file.size = value;
            else
                out.problems.push_back("line " + to_string(number) + ": size \"" + size + "\" is not a number");
        }
        file.sha256 = lower(field("sha256"));
        file.disc = atoi(field("disc").c_str());

        const string key = kind + "\t" + title;
        auto found = byKey.find(key);
        if (found == byKey.end()) {
            StoreItem item;
            item.id = kind + "/" + title;
            item.kind = kind;
            item.title = title;
            item.source = out.name;
            byKey[key] = out.items.size();
            out.items.push_back(item);
            found = byKey.find(key);
        }
        StoreItem &item = out.items[found->second];
        item.files.push_back(file);
        auto fill = [&](string &target, const char *column) {
            if (target.empty())
                target = field(column);
        };
        fill(item.version, "version");
        fill(item.serial, "serial");
        fill(item.image, "image");
        fill(item.description, "description");
        fill(item.author, "author");
        fill(item.licence, "licence");
    }
    for (StoreItem &item : out.items) {
        item.source = out.name;
        stable_sort(item.files.begin(), item.files.end(),
                    [](const StoreFile &a, const StoreFile &b) { return a.disc < b.disc; });
    }
    return out;
}

bool StoreSourceTsv::load(const string &path, const string &fallbackName, StoreSourceTsv &out, string &error) {
    string text;
    if (!readAll(path, text)) {
        error = "cannot read " + path;
        return false;
    }
    out = parse(text, fallbackName);
    return true;
}

} // namespace ableem
