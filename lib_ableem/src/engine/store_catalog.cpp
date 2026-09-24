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

// a header's fields as the column names the parser knows: ours, and the other well-known list layout's
// (NoPayStation's: Title ID, Region, Name, PKG direct link, ..., File Size, SHA256) under their own names -
// there "Name" is the title, which it only is when the header has no "title" (in ours it is the file name)
vector<string> headerColumns(const vector<string> &fields) {
    bool hasTitle = false;
    for (const string &f : fields)
        hasTitle = hasTitle || lower(f) == "title";
    vector<string> columns;
    for (const string &f : fields) {
        string c = lower(f);
        if (c == "pkg direct link" || c == "link" || c == "download url")
            c = "url";
        else if (c == "file size")
            c = "size";
        else if (c == "title id")
            c = "serial";
        else if (c == "name" && !hasTitle)
            c = "title";
        columns.push_back(c);
    }
    return columns;
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
    map<string, vector<size_t>> byKey; // kind + "\t" + title -> the items of that title
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
        // the header: the first line with a "url" field (under any of its names), before any data
        if (columns.empty() && out.items.empty()) {
            vector<string> named = headerColumns(fields);
            if (find(named.begin(), named.end(), "url") != named.end()) {
                columns = named;
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

        // one item per kind and title - and per serial: a list with one line per regional release of the same
        // title (a Title ID each) makes one item each, while a line with no serial joins as another disc
        const string serial = field("serial");
        size_t index = out.items.size();
        for (size_t candidate : byKey[kind + "\t" + title]) {
            const string &known = out.items[candidate].serial;
            if (serial.empty() || known.empty() || known == serial) {
                index = candidate;
                break;
            }
        }
        if (index == out.items.size()) {
            StoreItem item;
            item.kind = kind;
            item.title = title;
            item.source = out.name;
            byKey[kind + "\t" + title].push_back(index);
            out.items.push_back(item);
        }
        StoreItem &item = out.items[index];
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
    // ids: <kind>/<title>, with /<serial> where two items share a title, and #n past that
    map<string, int> uses;
    for (const StoreItem &item : out.items)
        uses[item.kind + "/" + item.title]++;
    map<string, int> seen;
    for (StoreItem &item : out.items) {
        item.source = out.name;
        item.id = item.kind + "/" + item.title;
        if (uses[item.id] > 1 && !item.serial.empty())
            item.id += "/" + item.serial;
        if (++seen[item.id] > 1)
            item.id += "#" + to_string(seen[item.id]);
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
