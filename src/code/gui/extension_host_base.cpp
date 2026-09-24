//
// ExtensionHostBase - see the header.
//
#include "extension_host_base.h"
#include "app_base.h"
#include "core/main.h"
#include "core/services/system.h"

using namespace std;

//*******************************
// ExtensionLogAppender::write
//*******************************
void ExtensionLogAppender::write(const plog::Record &record) {
    plog::Logger<0> *launcher = plog::get<0>();
    if (launcher == nullptr)
        return;
    plog::Record tagged(record.getSeverity(), record.getFunc(), record.getLine(), record.getFile(),
                        record.getObject(), 0);
    tagged << "[" << tag_ << "] " << record.getMessage();
    launcher->write(tagged);
}

//*******************************
// ExtensionHostBase::ExtensionHostBase
//*******************************
ExtensionHostBase::ExtensionHostBase(AppBase &app, const ExtensionInfo &extension, const string &stateRoot)
    : app_(app), name_(extension.name), folder_(extension.folder), stateDir_(stateRoot + sep + extension.name),
      appender_(extension.name) {
    DirEntry::createDirs(stateDir_);
    // its own translations over the launcher's: <folder>/lang/<the current language>.txt
    const string lang = folder_ + sep + "lang";
    if (DirEntry::isDirectory(lang))
        app.lang().loadMore(lang);
}

//*******************************
// ExtensionHostBase::networkUp / logSeverity
//*******************************
bool ExtensionHostBase::networkUp() {
    return System::hasDefaultRoute();
}

plog::Severity ExtensionHostBase::logSeverity() {
    plog::Logger<0> *launcher = plog::get<0>();
    return launcher != nullptr ? launcher->getMaxSeverity() : plog::info;
}

//*******************************
// ExtensionHostBase: the launcher's requests, which a plain host only logs
//*******************************
void ExtensionHostBase::requestRescan() {
    PLOG_INFO << "[" << name_ << "] asked for a rescan (no scan in this host)";
}

void ExtensionHostBase::reloadApps() {
    PLOG_INFO << "[" << name_ << "] asked for the Apps to be reloaded (no Apps in this host)";
}

void ExtensionHostBase::reloadConfig() {
    PLOG_INFO << "[" << name_ << "] asked for config.ini to be reloaded (not in this host)";
}

void ExtensionHostBase::notify(const string &title, const string &detail, uint64_t done, uint64_t total) {
    PLOG_DEBUG << "[" << name_ << "] " << title << ": " << detail << " " << done << "/" << total;
}

void ExtensionHostBase::clearNotification() {}
