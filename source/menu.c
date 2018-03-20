#include "menu.h"
#include "draw.h"
#include "hid.h"
#include "fs.h"
#ifdef USE_THEME
#include "theme.h"
#endif
#include "decryptor/nand.h"


#ifdef LOG_FILE
u32 ScrollOutput()
{
    u32 log_start = LogWrite(NULL);
    
    // careful, these areas are used by other functions in Hourglass9
    char** logptr = (char**) 0x21100000;
    char* logtext = (char*)  0x21200000;
    u32 log_size = FileGetData(LOG_FILE, logtext, 1024 * 1024, log_start); // log size
    u32 l_total = 0; // total lines
    u32 l_curr = 0; // current line
    
    // allow 1MB of text max
    if ((log_size == 0) || (log_size >= 1024 * 1024))
        return 0;
    
    // organize lines
    logtext[log_size - 1] = '\0';
    logptr[l_total++] = logtext;
    for (char* line = strchr(logtext, '\n'); line != NULL && l_total < 4000; line = strchr(line, '\n')) {
        *line = '\0';
        logptr[l_total++] = ++line; 
    }
    if (l_total >= 4000) // allow 4000 lines of text max
        return 0;
    for (; l_total < DBG_N_CHARS_Y; logptr[l_total++] = logtext + log_size - 1); // fill up with empty lines
    // here, the actual output starts
    l_curr = l_total - DBG_N_CHARS_Y;
    if (l_curr > 0) l_curr--; // start at the line before the last
    while (true) {
        DebugSet((const char**) logptr + l_curr);
        u32 pad_state = InputWait();
        if (pad_state & BUTTON_X) {
            Screenshot(NULL);
        } else if ((pad_state & BUTTON_UP) && (l_curr > 0)) {
            l_curr--;
        } else if ((pad_state & BUTTON_DOWN) && (l_curr < l_total - DBG_N_CHARS_Y)) {
            l_curr++;
        } else if (pad_state & (BUTTON_B | BUTTON_START | BUTTON_HOME | BUTTON_POWER)) {
            return pad_state;
        }
    }
    
    return 0;
}
#endif

u32 UnmountSd()
{
    u32 pad_state;
    
    #ifdef USE_THEME
    LoadThemeGfx(GFX_UNMOUNT, false);
    DeinitFS();
    #else
    DebugClear();
    Debug("Unmounting SD card...");
    DeinitFS();
    Debug("SD is unmounted, you may remove it now.");
    Debug("Put the SD card back in before pressing B!");
    Debug("");
    Debug("(B to return, START to reboot)");
    #endif
    while (true) {
        pad_state = InputWait();
        if (((pad_state & BUTTON_B) && InitFS()) || (pad_state & (BUTTON_START | BUTTON_HOME | BUTTON_POWER)))
            break;
    }
    
    return pad_state;
}

void DisplayOptionInfo(MenuEntry *entry)
{
    if (entry->info == NULL) return;

    u8 *fb_backup_loc = (u8*)(0x21100000);
    const u32 fb_backup_len = (320*240*3);
    memcpy(fb_backup_loc, BOT_SCREEN, fb_backup_len);

    ClearScreenFull(false, true);

    DrawStringFC(0, 10, false, COLOR_GREEN, "%s:", entry->name);
    DrawStringF(0, 20, false, entry->info);
    DrawStringF(0, 180, false, "For more information, please read\ngithub.com/d0k3/Decrypt9WIP/tree/master/README.md");
    DrawStringF(0, 220, false, "Release Y to continue.");
    while (CheckButton(BUTTON_Y));

    memcpy(BOT_SCREEN, fb_backup_loc, fb_backup_len);

    return;
}

void DrawMenu(MenuInfo* currMenu, u32 index, bool fullDraw, bool subMenu)
{
    u32 emunand_state = CheckEmuNand();
    bool top_screen = true;
    u32 menublock_x0 = (((top_screen) ? SCREEN_WIDTH_TOP : SCREEN_WIDTH_BOT) - 30 * FONT_WIDTH_EXT) / 2;
    u32 menublock_x1 = menublock_x0;
    u32 menublock_y0 = 60;
    u32 menublock_y1 = menublock_y0 + currMenu->n_entries * 10;
    
    if (fullDraw) { // draw full menu
        ClearScreenFull(true, !top_screen);
        DrawStringF(menublock_x0, menublock_y0 - 20, top_screen, "%s", currMenu->name);
        DrawStringF(menublock_x0, menublock_y0 - 10, top_screen, "==============================");
        DrawStringF(menublock_x0, menublock_y1 +  0, top_screen, "==============================");
        DrawStringF(menublock_x0, menublock_y1 + 10, top_screen, (subMenu) ? "A: Choose  B: Return" : "A: Choose");
        DrawStringF(menublock_x0, menublock_y1 + 20, top_screen, "SELECT : Unmount SD");
        DrawStringF(menublock_x0, menublock_y1 + 30, top_screen, "START  : Reboot");
        DrawStringF(menublock_x0, menublock_y1 + 40, top_screen, "START+\x1B: Poweroff");
        DrawStringF(menublock_x1, SCREEN_HEIGHT - 20, top_screen, "SD storage: %lluMB / %lluMB", RemainingStorageSpace() / (1024*1024), TotalStorageSpace() / (1024*1024));
        DrawStringF(menublock_x1, SCREEN_HEIGHT - 30, top_screen, "EmuNAND: %s",
            (emunand_state == EMUNAND_NOT_READY) ? "SD not ready" :
            (emunand_state == EMUNAND_GATEWAY) ? "GW EmuNAND" : 
            (emunand_state == EMUNAND_REDNAND) ? "RedNAND" : "SD is ready" );
        DrawStringF(menublock_x1, SCREEN_HEIGHT - 30, top_screen, "Work directory: %s", GetWorkDir());
    }
    
    if (!top_screen)
        DrawStringF(10, 10, true, "Selected: %-*.*s", 32, 32, currMenu->entries[index].name);
        
    for (u32 i = 0; i < currMenu->n_entries; i++) { // draw menu entries / selection []
        char* name = currMenu->entries[i].name;
        DrawStringFC(menublock_x0, menublock_y0 + (i*10), top_screen, (i == index) ? COLOR_ASK : COLOR_SELECT, 
            (i == index) ? "[%s]" : " %s ", name);
    }
}

u32 ProcessEntry(MenuEntry* entry)
{
    bool emunand    = entry->param & N_EMUNAND;
    bool nand_force = entry->param & N_FORCEEMU;
    bool warning    = entry->param & N_NANDWRITE;
    u32 entryColor = COLOR_GREEN;
    
    u32 pad_state;
    u32 res = 0;
    
    // unlock sequence for dangerous features
    if (warning) {
        u32 unlockSequenceEmu[] = { BUTTON_LEFT, BUTTON_RIGHT, BUTTON_DOWN, BUTTON_UP, BUTTON_A };
        u32 unlockSequenceSys[] = { BUTTON_LEFT, BUTTON_UP, BUTTON_RIGHT, BUTTON_UP, BUTTON_A };
        u32 unlockLvlMax = ((emunand) ? sizeof(unlockSequenceEmu) : sizeof(unlockSequenceSys)) / sizeof(u32);
        u32* unlockSequence = (emunand) ? unlockSequenceEmu : unlockSequenceSys;
		
        u32 unlockLvl = 0;
        entryColor = (emunand) ? COLOR_YELLOW : COLOR_RED;
        #ifdef USE_THEME
        LoadThemeGfx((emunand) ? GFX_DANGER_E : GFX_DANGER_S, false);
        #endif
        DebugClear();
        Debug("You selected \"%s\".", entry->name);
        DebugColor(entryColor, "This feature writes to the %s.", (emunand) ? "EmuNAND" : "SysNAND");
        DebugColor(entryColor, "Data will be overwriten, keep backups!");
        Debug("");
        Debug("If you wish to proceed, enter:");
        Debug((emunand) ? "<Left>, <Right>, <Down>, <Up>, <A>" : "<Left>, <Up>, <Right>, <Up>, <A>");
        Debug("");
        Debug("(B to return, START to reboot)");
        while (true) {
            ShowProgress(unlockLvl, unlockLvlMax);
            if (unlockLvl == unlockLvlMax)
                break;
            pad_state = InputWait();
            if (!(pad_state & (BUTTON_ANY | BUTTON_HOME | BUTTON_POWER)))
                continue;
            else if (pad_state & unlockSequence[unlockLvl])
                unlockLvl++;
            else if (pad_state & (BUTTON_B | BUTTON_START | BUTTON_HOME | BUTTON_POWER))
                break;
            else if (unlockLvl == 0 || !(pad_state & unlockSequence[unlockLvl-1]))
                unlockLvl = 0;
        }
        ShowProgress(0, 0);
        if (unlockLvl < unlockLvlMax)
            return pad_state;
    }
    
    // execute this entries function
    #ifdef USE_THEME
    LoadThemeGfx(GFX_PROGRESS, false);
    #endif
    DebugClear();
    DebugColor(entryColor, "Selected: [%s]", entry->name);
    res = (SetNand(emunand, nand_force) == 0) ? (*(entry->function))(entry->param) : 1;
    DebugColor((res == 0) ? COLOR_GREEN : COLOR_RED, "%s: %s!", entry->name, (res == 0) ? "succeeded" : "failed");
    Debug("");
    Debug("Press B to return, START to reboot.");
    #ifdef USE_THEME
    LoadThemeGfx((res == 0) ? GFX_DONE : GFX_FAILED, false);
    #endif
    while(!((pad_state = InputWait()) & (BUTTON_B | BUTTON_START | BUTTON_HOME | BUTTON_POWER))) {
        if (pad_state & BUTTON_X) Screenshot(NULL);
        #ifdef LOG_FILE
        else if (pad_state & BUTTON_UP) {
            pad_state = ScrollOutput();
            break;
        }
        #endif
    }
    
    // returns the last known pad_state
    return pad_state;
}

void BatchScreenshot(MenuInfo* info, bool full_batch)
{
    for (u32 idx_m = 0; info[idx_m].name != NULL; idx_m++) {
        for (u32 idx_s = 0; idx_s < ((full_batch) ? info[idx_m].n_entries : 1); idx_s++) {
            char filename[16];
            snprintf(filename, 16, "menu%04lu.bmp", (idx_m * 100) + idx_s);
            #ifndef USE_THEME
            DrawMenu(info + idx_m, idx_s, true, true);
            #else
            LoadThemeGfxLogo();
            LoadThemeGfxMenu((idx_m * 100) + idx_s);
            #endif
            Screenshot(filename);
        }
    }
}

u32 ProcessMenu(MenuInfo* info, u32 n_entries_main)
{
    MenuInfo* currMenu;
    MenuInfo* prevMenu[MENU_MAX_DEPTH];
    u32 prevIndex[MENU_MAX_DEPTH];
    u32 menuLvlMin;
    u32 menuLvl;
    u32 index = 0;
    u32 result = MENU_EXIT_REBOOT;
    
    #ifndef USE_THEME
    MenuInfo mainMenu;
    if (n_entries_main > 1) {
        // build main menu structure from submenus
        if (n_entries_main > MENU_MAX_ENTRIES) // limit number of entries
            n_entries_main = MENU_MAX_ENTRIES;
        memset(&mainMenu, 0x00, sizeof(MenuInfo));
        for (u32 i = 0; i < n_entries_main; i++) {
            mainMenu.entries[i].name = info[i].name;
            mainMenu.entries[i].function = NULL;
            mainMenu.entries[i].param = i;
        }
        mainMenu.n_entries = n_entries_main;
        #ifndef BUILD_NAME
        mainMenu.name = "Hourglass9 Main Menu";
        #else
        mainMenu.name = BUILD_NAME;
        #endif
        currMenu = &mainMenu;
        menuLvlMin = 0;
    } else {
        currMenu = info;
        menuLvlMin = 1;
    }
    DrawMenu(currMenu, 0, true, false);
    #else
    currMenu = info;
    menuLvlMin = 1;
    LoadThemeGfxLogo();
    LoadThemeGfxMenu(0);
    #endif
    menuLvl = menuLvlMin;
    
    // main processing loop
    while (true) {
        bool full_draw = true;
        u32 pad_state = InputWait();
        if ((pad_state & BUTTON_A) && (currMenu->entries[index].function == NULL)) {
            if (menuLvl < MENU_MAX_DEPTH) {
                prevMenu[menuLvl] = currMenu;
                prevIndex[menuLvl] = index;
                menuLvl++;
            }
            currMenu = info + currMenu->entries[index].param;
            index = 0;
        } else if (pad_state & BUTTON_A) {
            pad_state = ProcessEntry(currMenu->entries + index);
        } else if ((pad_state & BUTTON_B) && (menuLvl > menuLvlMin)) {
            menuLvl--;
            currMenu = prevMenu[menuLvl];
            index = prevIndex[menuLvl];
        } else if (pad_state & BUTTON_DOWN) {
            index = (index == currMenu->n_entries - 1) ? 0 : index + 1;
            full_draw = false;
        } else if (pad_state & BUTTON_UP) {
            index = (index == 0) ? currMenu->n_entries - 1 : index - 1;
            full_draw = false;
        } else if ((pad_state & BUTTON_R1) && (menuLvl == 1)) {
            if (++currMenu >= info + n_entries_main) currMenu = info;
            index = 0;
        } else if ((pad_state & BUTTON_L1) && (menuLvl == 1)) {
            if (--currMenu < info) currMenu = info + n_entries_main - 1;
            index = 0;
        } else if (pad_state & BUTTON_SELECT) {
            pad_state = UnmountSd();
        } else if (pad_state & BUTTON_X) {
            (pad_state & (BUTTON_LEFT | BUTTON_RIGHT)) ?
                BatchScreenshot(info, pad_state & BUTTON_RIGHT) : Screenshot(NULL);
        } else if (pad_state & BUTTON_Y) {
            DisplayOptionInfo(currMenu->entries + index);
            full_draw = false;
        } else {
            full_draw = false;
        }
        if (pad_state & BUTTON_START) {
            result = (pad_state & BUTTON_LEFT) ? MENU_EXIT_POWEROFF : MENU_EXIT_REBOOT;
            break;
        } else if (pad_state & BUTTON_POWER) {
            result = MENU_EXIT_POWEROFF;
            break;
        } else if (pad_state & BUTTON_HOME) {
            result = MENU_EXIT_REBOOT;
            break;
        }
        #ifndef USE_THEME
        DrawMenu(currMenu, index, full_draw, menuLvl > menuLvlMin);
        #else
        if (full_draw) LoadThemeGfxLogo();
        LoadThemeGfxMenu(((currMenu - info) * 100) + index);
        #endif
    }
    
    return result;
}
