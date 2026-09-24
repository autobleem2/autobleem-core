// 64-bit file offsets on 32-bit Linux (the console, the Pi): an entry unpacked with progress may be a
// partition image bigger than 2 GB
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#endif

#include "ableem/engine/zip_archive.h"
#include "ableem/engine/filesystem.h"

#include <cstdio>
#include <iostream>
#include <miniz.h>
#include "ableem/engine/log.h"
#include <ableem/engine/log.h>

using namespace std;

namespace ableem {

namespace {

// creates every level of `dir`, like mkdir -p; true if it exists afterwards
bool createDirs(const string &dir) {
    if (dir.empty() || DirEntry::isDirectory(dir))
        return true;
    size_t slash = dir.find_last_of('/');
    if (slash != string::npos && slash > 0 && !createDirs(dir.substr(0, slash)))
        return false;
    DirEntry::createDir(dir);
    return DirEntry::isDirectory(dir);
}

// a reader over one file, closed on the way out
struct Reader {
    mz_zip_archive zip;
    bool open = false;

    explicit Reader(const string &path) : zip() {
        mz_zip_zero_struct(&zip);
        open = mz_zip_reader_init_file(&zip, path.c_str(), 0) != 0;
        if (!open) {
            PLOG_WARNING << "Not a zip file: " << path << " (" << mz_zip_get_error_string(mz_zip_get_last_error(&zip))
                         << ")";
        }
    }
    ~Reader() {
        if (open)
            mz_zip_reader_end(&zip);
    }

    unsigned count() { return mz_zip_reader_get_num_files(&zip); }

    bool name(unsigned i, string &out, bool &isDir) {
        ZipEntry entry;
        if (!stat(i, entry))
            return false;
        out = entry.name;
        isDir = entry.isDir;
        return true;
    }

    bool stat(unsigned i, ZipEntry &entry) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st))
            return false;
        entry.name = st.m_filename;
        entry.isDir = mz_zip_reader_is_file_a_directory(&zip, i) != 0;
        if (entry.isDir && (entry.name.empty() || entry.name.back() != '/'))
            entry.name += '/';
        entry.crc = st.m_crc32;
        entry.size = st.m_uncomp_size;
        return true;
    }
};

// what miniz's extract callback writes to: the target file, and the whole extraction's progress
struct Sink {
    FILE *file;
    uint64_t done;
    uint64_t total;
    const ByteProgress *progress;
};

size_t writeChunk(void *opaque, mz_uint64 offset, const void *buffer, size_t size) {
    Sink *sink = static_cast<Sink *>(opaque);
    (void)offset; // the output is streamed; offsets only ever grow by `size`
    size_t written = fwrite(buffer, 1, size, sink->file);
    sink->done += written;
    if (written > 0)
        (*sink->progress)(sink->done, sink->total);
    return written;
}

} // namespace

//*******************************
// ZipArchive::isSafeName
//*******************************
bool ZipArchive::isSafeName(const string &name) {
    if (name.empty())
        return false;
    if (name[0] == '/' || name.find('\\') != string::npos || name.find(':') != string::npos)
        return false;
    // no "." or ".." segment anywhere
    size_t start = 0;
    while (start <= name.size()) {
        size_t end = name.find('/', start);
        if (end == string::npos)
            end = name.size();
        string segment = name.substr(start, end - start);
        if (segment == ".." || segment == ".")
            return false;
        start = end + 1;
    }
    return true;
}

//*******************************
// ZipArchive::list
//*******************************
bool ZipArchive::list(const string &zipPath, vector<string> &names) {
    names.clear();
    Reader reader(zipPath);
    if (!reader.open)
        return false;
    for (unsigned i = 0; i < reader.count(); i++) {
        string name;
        bool isDir;
        if (!reader.name(i, name, isDir))
            continue;
        names.push_back(name);
    }
    return true;
}

//*******************************
// ZipArchive::listEntries
//*******************************
bool ZipArchive::listEntries(const string &zipPath, vector<ZipEntry> &entries) {
    entries.clear();
    Reader reader(zipPath);
    if (!reader.open)
        return false;
    for (unsigned i = 0; i < reader.count(); i++) {
        ZipEntry entry;
        if (reader.stat(i, entry))
            entries.push_back(entry);
    }
    return true;
}

//*******************************
// ZipArchive::extract
//*******************************
bool ZipArchive::extract(const string &zipPath, const string &destDir) {
    return extract(zipPath, destDir, ByteProgress());
}

bool ZipArchive::extract(const string &zipPath, const string &destDir, const ByteProgress &progress) {
    Reader reader(zipPath);
    if (!reader.open)
        return false;

    // check every name before writing anything, and add up what is to be written
    uint64_t total = 0;
    for (unsigned i = 0; i < reader.count(); i++) {
        ZipEntry entry;
        if (!reader.stat(i, entry)) {
            PLOG_INFO << "Zip entry " << i << " of " << zipPath << " is unreadable";
            return false;
        }
        if (!isSafeName(entry.name)) {
            PLOG_INFO << "Refusing zip entry '" << entry.name << "' in " << zipPath;
            return false;
        }
        if (!entry.isDir)
            total += entry.size;
    }
    Sink sink{nullptr, 0, total, &progress};

    if (!createDirs(destDir)) {
        PLOG_WARNING << "Could not create " << destDir;
        return false;
    }
    const string dest = destDir + sep;
    for (unsigned i = 0; i < reader.count(); i++) {
        string name;
        bool isDir;
        reader.name(i, name, isDir);
        const string target = dest + name;
        if (isDir) {
            if (!createDirs(DirEntry::removeSeparatorFromEndOfPath(target)))
                return false;
            continue;
        }
        size_t slash = target.find_last_of('/');
        if (slash != string::npos && !createDirs(target.substr(0, slash))) {
            PLOG_WARNING << "Could not create the directory for " << target;
            return false;
        }
        bool ok;
        if (!progress) {
            ok = mz_zip_reader_extract_to_file(&reader.zip, i, target.c_str(), 0) != 0;
        } else {
            // the same through miniz's streaming callback, so every chunk can be reported
            sink.file = fopen(target.c_str(), "wb");
            if (!sink.file) {
                PLOG_ERROR << "Could not create " << target;
                return false;
            }
            ok = mz_zip_reader_extract_to_callback(&reader.zip, i, writeChunk, &sink, 0) != 0;
            ok = fclose(sink.file) == 0 && ok;
        }
        if (!ok) {
            PLOG_ERROR << "Could not extract " << name << " from " << zipPath << " ("
                       << mz_zip_get_error_string(mz_zip_get_last_error(&reader.zip)) << ")";
            return false;
        }
    }
    return true;
}

} // namespace ableem
