//
// ExtensionHostBase: the part of ExtensionHost every host shares - the extension's folder and state directory,
// the network, and its log lines tagged and handed to the launcher's log. The launcher derives from it and
// supplies what only it knows (its scan, its Apps set, its notification bubble); on its own the requests are
// logged and dropped.
//
#pragma once

#include "extension.h"
#include "core/services/extension_catalog.h"

#include <string>

//******************
// ExtensionLogAppender
//******************
// Forwards an extension's record to the launcher's logger (plog instance 0) with "[<name>] " in front of it.
class ExtensionLogAppender : public plog::IAppender {
public:
    explicit ExtensionLogAppender(std::string tag) : tag_(std::move(tag)) {}
    void write(const plog::Record &record) override;

private:
    std::string tag_;
};

//******************
// ExtensionHostBase
//******************
class ExtensionHostBase : public ExtensionHost {
public:
    ExtensionHostBase(AppBase &app, const ExtensionInfo &extension, const std::string &stateRoot);

    AppBase &app() override { return app_; }
    const std::string &name() const override { return name_; }
    const std::string &folder() const override { return folder_; }
    const std::string &stateDir() const override { return stateDir_; }
    bool networkUp() override;

    void requestRescan() override;
    void reloadApps() override;
    void reloadConfig() override;
    void notify(const std::string &title, const std::string &detail, uint64_t done, uint64_t total) override;
    void clearNotification() override;

    plog::IAppender *logAppender() override { return &appender_; }
    plog::Severity logSeverity() override;

private:
    AppBase &app_;
    std::string name_, folder_, stateDir_;
    ExtensionLogAppender appender_;
};
