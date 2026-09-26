//
// Which games the carousel is showing, and where in them the cursor is.
//
#pragma once

#include <string>

// the carousel's "set". if you add one, also update GuiLauncher::showSetName's setNames and GameSetLast.
// Lightgun (2026-09-18, from AutoBleem-NG) is every PS1 and RetroArch game flagged as a light-gun game;
// the launcher skips it in the Select cycle while it is empty.
enum class GameSet : int { PS1 = 0, RetroArch = 1, Lightgun = 2, Apps = 3 };
constexpr GameSet GameSetLast = GameSet::Apps;

// cycles PS1 -> RetroArch -> Lightgun -> Apps -> PS1, the order Select steps through the sets in.
inline GameSet nextGameSet(GameSet set) {
    int next = static_cast<int>(set) + 1;
    return next > static_cast<int>(GameSetLast) ? GameSet::PS1 : static_cast<GameSet>(next);
}

// GameSet::PS1 sub-states. keep GamesSubdir last: it is left off the L2+Select menu.
enum class Ps1SelectState : int { AllGames = 0, InternalOnly, Favorites, History, GamesSubdir };

// An App's app.ini `Category=` (2026-09-26, docs/app-format-plan.md), case-insensitive; anything else or
// missing is Other. All is not a category an app.ini can name - it is the picker's "every app" row.
// Keep the order Games/Emulators/Tools/Media/Other: it is the order the set picker and appCategories() list
// them in.
enum class AppCategory : int { All = 0, Games, Emulators, Tools, Media, Other };
constexpr AppCategory AppCategoryLast = AppCategory::Other;

// the untranslated English name - translate at the call site with _(); GameQueryService::apps()'s parser is
// the read side of this table.
inline std::string appCategoryName(AppCategory category) {
    switch (category) {
    case AppCategory::All:
        return "All apps";
    case AppCategory::Games:
        return "Games";
    case AppCategory::Emulators:
        return "Emulators";
    case AppCategory::Tools:
        return "Tools";
    case AppCategory::Media:
        return "Media";
    case AppCategory::Other:
        return "Other";
    }
    return "Other";
}

//******************
// GameSetSelection
//******************
// Everything that says "the carousel is showing these games, positioned here". GuiLauncher works on its own
// copy while it runs and hands it to the Session when a game starts, so pressing Start later reopens the
// carousel in the same place. Before this was six loose fields mirrored on both sides and copied across by
// hand at four call sites.
struct GameSetSelection {
    GameSet set = GameSet::PS1;
    Ps1SelectState ps1SelectState = Ps1SelectState::AllGames;
    int gameIndex = 0;       // index into GuiLauncher::carouselGames
    int usbGameDirIndex = 0; // row 0 of the game-dir menu is /Games itself
    std::string usbGameDirName;
    int raPlaylistIndex = 0; // row 0 is the first playlist name
    std::string raPlaylistName;
    AppCategory appCategory = AppCategory::All; // the Apps tab's row; All = every App
};
