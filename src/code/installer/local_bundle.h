//
// LocalBundle: a Downloader over the payload folder of an installer download that carries its own packs
// (AutoBleemInstaller-<v>-full.zip, the folder `payload/` next to the exe). The folder holds the files the site
// would serve - psc/cores/<pack>.tar.gz, psc/retroarch/latest.json, assets/frontend/assets.zip, the stick package
// - at the very paths of their site URLs, and bundle.json (BundleCatalog) lists each with its size and sha256.
// A URL whose path is in the manifest is answered from the folder; anything else (the BIOS list and its files,
// UpdateRoms of another release) goes to the downloader behind it, so the online mode keeps working for what the
// bundle does not carry, and a bundle needs no internet for what it does.
//
// Each file is checked once against the manifest (size, then sha256), the first time it is asked for; localFile()
// then hands out its path so the job reads the pack in place instead of copying a few hundred MB first.
//
#pragma once

#include "installer/installer_job.h"

#include <ableem/engine/update_catalog.h>

#include <mutex>
#include <set>
#include <string>

class LocalBundle : public Downloader {
public:
    // `dir`: the folder with bundle.json; `inner`: what answers for URLs the bundle does not hold (may be null:
    // such a URL then fails as "not in this download")
    LocalBundle(const std::string &dir, Downloader *inner);

    // reads bundle.json; false with `error` when it is missing or not a manifest
    bool load(std::string &error);
    const ableem::BundleCatalog &catalog() const { return catalog_; }
    // dir/<rel>
    std::string pathOf(const std::string &rel) const { return dir_ + "/" + rel; }
    // "https://host/a/b%20c?x=1" -> "a/b c": the part of a URL the manifest is keyed by
    static std::string urlPath(const std::string &url);

    bool fetch(const std::string &url, const std::string &destFile, const Progress &progress,
               std::string &error) override;
    bool fetchResumable(const std::string &url, const std::string &destFile, const Progress &progress,
                        std::string &error) override;
    int connections() const override { return inner_ ? inner_->connections() : 1; }
    bool localFile(const std::string &url, std::string &path, const Progress &progress, std::string &error) override;
    // the same by the manifest's path ("autobleem-psc-v1.tar.gz"): checked once, then its path; false with `error`
    // empty when the manifest has no such file
    bool checkedPath(const std::string &rel, std::string &path, const Progress &progress, std::string &error);

private:
    // true when `url` is one of the bundle's files and it checks out (once); false with `error` empty when it is
    // not the bundle's, set when it is and does not check out
    bool held(const std::string &url, std::string &path, const Progress &progress, std::string &error);
    bool verify(const ableem::BundleFile &file, const Progress &progress, std::string &error);

    std::string dir_;
    Downloader *inner_;
    ableem::BundleCatalog catalog_;
    std::mutex m_;
    std::set<std::string> verified_;
};
