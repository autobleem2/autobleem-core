//
// LocalBundle - see the header.
//
#include "installer/local_bundle.h"

#include <ableem/engine/filesystem.h>
#include <ableem/engine/sha256.h>

#include <fstream>

using namespace std;
using ableem::BundleFile;
using ableem::DirEntry;
using ableem::Sha256;

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

LocalBundle::LocalBundle(const string &dir, Downloader *inner) : dir_(dir), inner_(inner) {
    while (dir_.size() > 1 && (dir_.back() == '/' || dir_.back() == '\\'))
        dir_.pop_back();
}

//*******************************
// LocalBundle::load
//*******************************
bool LocalBundle::load(string &error) {
    ifstream in(pathOf("bundle.json"), ios::binary);
    if (!in) {
        error = "no bundle.json in " + dir_;
        return false;
    }
    const string text((istreambuf_iterator<char>(in)), istreambuf_iterator<char>());
    if (!catalog_.parse(text)) {
        error = dir_ + "/bundle.json is not a bundle manifest";
        return false;
    }
    return true;
}

//*******************************
// LocalBundle::urlPath
//*******************************
string LocalBundle::urlPath(const string &url) {
    size_t at = url.find("://");
    at = at == string::npos ? 0 : url.find('/', at + 3);
    if (at == string::npos)
        return "";
    string path = url.substr(at);
    path = path.substr(0, path.find_first_of("?#"));
    string out;
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '%' && i + 2 < path.size() && hexValue(path[i + 1]) >= 0 && hexValue(path[i + 2]) >= 0) {
            out += static_cast<char>(hexValue(path[i + 1]) * 16 + hexValue(path[i + 2]));
            i += 2;
        } else {
            out += path[i];
        }
    }
    size_t start = 0;
    while (start < out.size() && out[start] == '/')
        start++;
    return out.substr(start);
}

//*******************************
// LocalBundle::verify
//*******************************
// size first (a cut-off download is caught without reading it), then the sha256, streamed with the progress
bool LocalBundle::verify(const BundleFile &file, const Progress &progress, string &error) {
    const string path = pathOf(file.path);
    const long long size = DirEntry::fileSize(path);
    if (size < 0) {
        error = file.path + " is missing from this download - unpack the whole zip again";
        return false;
    }
    if (static_cast<uint64_t>(size) != file.size) {
        error = file.path + " has the wrong size (" + to_string(size) + " bytes, expected " + to_string(file.size) +
                ") - unpack the whole zip again";
        return false;
    }
    ifstream in(path, ios::binary);
    if (!in) {
        error = "cannot read " + path;
        return false;
    }
    Sha256 sha;
    static thread_local char buffer[256 * 1024];
    uint64_t done = 0;
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        sha.update(reinterpret_cast<const unsigned char *>(buffer), static_cast<size_t>(in.gcount()));
        done += static_cast<uint64_t>(in.gcount());
        if (progress && !progress(done, file.size)) {
            error = "Stopped";
            return false;
        }
    }
    if (sha.hexDigest() != file.sha256) {
        error = file.path + " does not match the checksum in bundle.json - unpack the whole zip again";
        return false;
    }
    return true;
}

//*******************************
// LocalBundle::held
//*******************************
bool LocalBundle::held(const string &url, string &path, const Progress &progress, string &error) {
    return checkedPath(urlPath(url), path, progress, error);
}

bool LocalBundle::checkedPath(const string &rel, string &path, const Progress &progress, string &error) {
    const BundleFile *file = catalog_.find(rel);
    if (!file)
        return false;
    {
        lock_guard<mutex> lock(m_);
        if (verified_.count(file->path)) {
            path = pathOf(file->path);
            return true;
        }
    }
    if (!verify(*file, progress, error))
        return false; // `error` says why; held-but-bad is not "not ours"
    lock_guard<mutex> lock(m_);
    verified_.insert(file->path);
    path = pathOf(file->path);
    return true;
}

//*******************************
// LocalBundle::localFile
//*******************************
bool LocalBundle::localFile(const string &url, string &path, const Progress &progress, string &error) {
    error.clear();
    return held(url, path, progress, error);
}

//*******************************
// LocalBundle::fetch
//*******************************
// a bundle file copied to `destFile`: it was checked on the way in, so the copy is not hashed again
bool LocalBundle::fetch(const string &url, const string &destFile, const Progress &progress, string &error) {
    string path;
    error.clear();
    if (held(url, path, progress, error)) {
        const long long size = DirEntry::fileSize(path);
        if (!DirEntry::copyFile(path, destFile)) {
            error = "cannot copy " + path + " to " + destFile;
            return false;
        }
        if (progress && !progress(static_cast<uint64_t>(size), static_cast<uint64_t>(size))) {
            error = "Stopped";
            return false;
        }
        return true;
    }
    if (!error.empty())
        return false;
    if (!inner_) {
        error = url + ": not in this download";
        return false;
    }
    return inner_->fetch(url, destFile, progress, error);
}

//*******************************
// LocalBundle::fetchResumable
//*******************************
bool LocalBundle::fetchResumable(const string &url, const string &destFile, const Progress &progress, string &error) {
    if (catalog_.find(urlPath(url)) || !inner_)
        return fetch(url, destFile, progress, error);
    return inner_->fetchResumable(url, destFile, progress, error);
}
