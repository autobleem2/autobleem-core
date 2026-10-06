//
// ScanScope: which part of the library a scan has to look at. A scan asked for by the Store, or by the watcher
// after one folder changed, covers only what could have changed - installing an App never rescans the PS1 games.
// A bit mask of the Scan* values; ScanAll is what every caller that does not know gets (the same full scan as
// always). The one definition the scanner (ScanService) and an extension's host interface (requestRescan) share.
//
#pragma once

typedef unsigned ScanScope;

constexpr ScanScope ScanNone = 0;
constexpr ScanScope ScanApps = 1;      // the Apps set is reloaded, nothing is scanned
constexpr ScanScope ScanMods = 2;      // Mods/ through the mods processors (a PE package), then the Apps set
constexpr ScanScope ScanPs1 = 4;       // Games/: the PS1 processors, the game scan, the box art
constexpr ScanScope ScanRoms = 8;      // roms/: the ROMs processors, the RetroArch ROM scan and its playlists
constexpr ScanScope ScanPackages = 16; // Packages/ (and the engines' own data folders): the RAM index is rebuilt
constexpr ScanScope ScanAll = ScanApps | ScanMods | ScanPs1 | ScanRoms | ScanPackages;
