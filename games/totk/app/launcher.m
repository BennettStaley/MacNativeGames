// The launcher inside the packaged app (build_app.py compiles it as Contents/MacOS/TOTK).
// The game itself is Contents/Resources/totk/build/totk-native; everything it writes goes to the player's own folders:
//   ~/Library/Application Support/MacNativeGames/TOTK/   profile (account, saves), shader cache, game slot, RomFS proof
//                                                       and game-data/: the RomFS image and prepared shaders the app
//                                                       points at (game-data.json; read-only for the game)
//   ~/Library/Logs/MacNativeGames/totk.log             this run (totk.previous.log: the one before)
// If the game is already running, the launcher only brings it to the front.
#import <AppKit/AppKit.h>
#include <sys/sysctl.h>
#include <errno.h>
#include <fcntl.h>
#include <libproc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <unistd.h>
#include "dmsh_chain.h"   // DMSH_CHAIN: the shader translation chain the cache folder is named after
#include "game_version.h"
#include "controller_map.h"
#include "settings_schema.h"
#include "include/totk_ui_lang.h"
#include "totk_shader_contrib.h"   // the shared shader list contribution and its reminder

static void show_error(NSString *detail) {
    NSAlert *alert = [NSAlert new];
    alert.messageText = @"The game could not start";
    alert.informativeText = detail;
    [alert runModal];
}

static NSDictionary<NSString *, NSString *> *kv_read(NSString *path) {
    NSMutableDictionary *out = [NSMutableDictionary dictionary];
    NSString *text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    for (NSString *line in [text componentsSeparatedByString:@"\n"]) {
        NSRange eq = [line rangeOfString:@"="];
        if (eq.location == NSNotFound) continue;
        NSString *key = [line substringToIndex:eq.location];
        if (!out[key]) out[key] = [line substringFromIndex:eq.location + 1];
    }
    return out;
}

static uint64_t pid_start(pid_t pid) {   // 0 if gone or a zombie; with the pid it names one process exactly
    struct proc_bsdinfo bi;
    if (pid <= 0 || proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, sizeof bi) != (int)sizeof bi || bi.pbi_status == SZOMB) return 0;
    return bi.pbi_start_tvsec;
}

/* The game slot (runtime/game_slot.c): the running game holds an exclusive lock on <slot>/lock and names itself in
   <slot>/holder. Returns the running game's pid, or 0. */
static pid_t running_game(NSString *slot) {
    int fd = open([slot stringByAppendingPathComponent:@"lock"].fileSystemRepresentation, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    int held = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd);
    if (!held) return 0;
    NSDictionary *h = kv_read([slot stringByAppendingPathComponent:@"holder"]);
    /* A player launch must reach runtime/game_slot.c to preempt an agent's
       test. Only an existing player game (or another test for a test request)
       is brought forward here. */
    const char *requestOwner = getenv("TOTK_SLOT_OWNER");
    if (![h[@"owner"] isEqualToString:@"user"] &&
        (!requestOwner || !*requestOwner || !strcmp(requestOwner, "user"))) return 0;
    pid_t pid = [h[@"game_pid"] intValue];
    return pid > 0 && pid_start(pid) == (uint64_t)[h[@"game_start"] longLongValue] ? pid : 0;
}

/* Every TOTK_CONTRIB_STEP shaders the shared list does not have yet (totk_shader_contrib.h), before the game starts: ask
   the player to send the file in a GitHub issue. "Send" opens the issue form and shows the file in Finder to drag in. */
static void contrib_reminder(NSString *path) {
    NSUInteger rows = totk_contrib_due(path);
    if (!rows) return;
    totk_contrib_reminded(path, rows);   // "next time": TOTK_CONTRIB_STEP rows later; "don't remind me again": never
    [NSApplication sharedApplication]; [NSApp activateIgnoringOtherApps:YES];
    NSAlert *alert = [NSAlert new];
    alert.messageText = @(TOTK_UI("期待你的贡献", "We would love your contribution"));
    alert.informativeText = [NSString stringWithFormat:@(TOTK_UI("你有 %lu 个着色器可以贡献：玩的时候遇到的、公共着色器清单里还没有的。它们记在一个小文件里，只有着色器的标识，不含游戏内容、存档或个人信息。把它发到 GitHub 加进公共清单，以后大家打包的 App 都会带上它们，玩到那里不再卡顿。\n\n点“发送”会打开 GitHub 的提交页面，并在访达里显示这个文件，把它拖进页面即可。",
                                      "You have %lu shaders to contribute: ones your game met that the shared shader list does not have yet. They are kept in a small file with shader identifiers only: no game data, saves or personal information. Sent to GitHub and added to the shared list, they come with everyone's next build, and nobody stutters there again.\n\n\"Send\" opens the GitHub form and shows the file in Finder: drag it into the form.")), (unsigned long)rows];
    [alert addButtonWithTitle:@(TOTK_UI("发送", "Send"))];
    [alert addButtonWithTitle:@(TOTK_UI("下次提醒", "Remind me next time"))];
    [alert addButtonWithTitle:@(TOTK_UI("不要提醒了", "Don't remind me again"))];
    NSModalResponse choice = [alert runModal];
    if (choice == NSAlertThirdButtonReturn) totk_contrib_never(path);
    if (choice != NSAlertFirstButtonReturn) return;
    [NSWorkspace.sharedWorkspace openURL:[NSURL URLWithString:@TOTK_CONTRIB_ISSUE_URL]];
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:path]]];
}

/* The game data outside the app (build_app.py's default Mac layout, docs/how-it-works.md "Where the game data lives"):
   Contents/Resources/totk/game-data.json names two content-addressed folders in
   ~/Library/Application Support/MacNativeGames/TOTK/game-data: <version>-romfs-<id> (romfs.bin, runtime-index.bin) and
   <version>-shaders-<id> (shader-seed/, shader-libs/). Each folder's store.json carries the same id, and the files the
   pointer lists must have their sizes; the game itself still checks the RomFS's SHA-256 (romfs.c: once per file
   identity, the record kept in that folder) and every prepared shader against the seed's manifest. An app built with
   --embed-data has no pointer and carries the data inside, as before. Returns nil when the data is there (and with
   apply, points the game at it), else what is wrong for the error dialog. */
static NSDictionary *json_read(NSString *path) {
    NSData *raw = [NSData dataWithContentsOfFile:path];
    id value = raw ? [NSJSONSerialization JSONObjectWithData:raw options:0 error:nil] : nil;
    return [value isKindOfClass:NSDictionary.class] ? value : nil;
}
static NSString *game_data(NSString *root, BOOL apply) {
    NSString *pointerPath = [root stringByAppendingPathComponent:@"game-data.json"];
    if (![NSFileManager.defaultManager fileExistsAtPath:pointerPath]) return nil;   // self-contained app (--embed-data)
    NSDictionary *pointer = json_read(pointerPath);
    NSString *rebuild = [pointer[@"rebuild"] isKindOfClass:NSString.class] ? pointer[@"rebuild"]
        : @"python3.14 build_app.py <game> -k <prod.keys> [--update <update>] -o <this app>";
    if (!pointer || [pointer[@"format"] intValue] != 1)
        return [NSString stringWithFormat:@"%@ is unreadable. Build the app again:\n\n%@", pointerPath, rebuild];
    NSMutableDictionary<NSString *, NSString *> *dirs = [NSMutableDictionary dictionary];
    for (NSString *part in @[@"romfs", @"shaders"]) {
        NSDictionary *want = pointer[part];
        NSString *dir = [want isKindOfClass:NSDictionary.class] && [want[@"path"] isKindOfClass:NSString.class] ? want[@"path"] : nil;
        NSString *problem = nil;
        if (!dir) problem = @"the app's game-data.json names no folder";
        else {
            NSDictionary *have = json_read([dir stringByAppendingPathComponent:@"store.json"]);
            if (!have) problem = @"the folder is missing (deleted, or the app came from another Mac)";
            else if (![have[@"id"] isEqual:want[@"id"]]) problem = @"the folder holds other data than this app was built with";
            NSDictionary *sizes = [want[@"sizes"] isKindOfClass:NSDictionary.class] ? want[@"sizes"] : @{};
            for (NSString *name in sizes) {
                if (problem) break;
                NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:[dir stringByAppendingPathComponent:name] error:nil];
                if (!a || a.fileSize != [sizes[name] unsignedLongLongValue])
                    problem = [NSString stringWithFormat:@"%@ is missing or has another size", name];
            }
        }
        if (problem)
            return [NSString stringWithFormat:@"The game data this app uses is not there: %@.\n\n%@\n\nBuild the app again; it writes the data "
                    @"there (in a minute or two when the build's work folder still has it):\n\n%@", problem, dir ?: pointerPath, rebuild];
        dirs[part] = dir;
    }
    if (apply) {
        setenv("TOTK_ROMFS", dirs[@"romfs"].fileSystemRepresentation, 1);
        setenv("TOTK_ROMFS_PROOF_DIR", dirs[@"romfs"].fileSystemRepresentation, 1);   // the verification record goes with the image
        setenv("TOTK_SHADER_SEED_DIR", [dirs[@"shaders"] stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation, 1);
        setenv("TOTK_SHADER_LIBS", [dirs[@"shaders"] stringByAppendingPathComponent:@"shader-libs"].fileSystemRepresentation, 1);
    }
    return nil;
}

static BOOL make_dir(NSString *path) {
    return [NSFileManager.defaultManager createDirectoryAtPath:path withIntermediateDirectories:YES attributes:nil error:nil];
}

int main(int argc, char **argv) {
    @autoreleasepool {
        NSString *root = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"totk"];
        NSString *binary = [root stringByAppendingPathComponent:@"build/totk-native"];
        /* --check (build_app.py, once after packaging): only look that the game is there and exit. The first run of a new
           app is when macOS scans the whole bundle (minutes for 16 GB, with nothing on screen); doing it during the build
           makes the player's first start immediate. */
        if (argc > 1 && !strcmp(argv[1], "--check")) {
            int ok = access(binary.fileSystemRepresentation, X_OK) == 0;
            NSString *declared = [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"];
            ok = ok && [declared isEqualToString:@TOTK_GAME_VERSION];
            NSString *data = ok ? game_data(root, NO) : nil;
            printf("%s: game version %s\n", !ok ? "missing binary or version mismatch" : data ? data.UTF8String : "ok", TOTK_GAME_VERSION);
            return ok && !data ? 0 : 1;
        }
        if (access(binary.fileSystemRepresentation, X_OK) != 0) {
            show_error(@"The game files are missing from this app. Build it again with build_app.py.");
            return 1;
        }
        NSString *missing = game_data(root, NO);
        if (missing) { show_error(missing); return 1; }
        NSString *home = NSHomeDirectory();
        NSString *data = [home stringByAppendingPathComponent:@"Library/Application Support/MacNativeGames/TOTK"];
        NSString *logs = [home stringByAppendingPathComponent:@"Library/Logs/MacNativeGames"];
        { const char *v = getenv("TOTK_LOG_DIR"); if (v && *v) logs = @(v); }   // a test's own log folder
        /* Folders the environment already names are kept (open --env TOTK_PROFILE_DIR=... runs a test on a save copy,
           TOTK_GAME_SLOT_DIR shares one game slot with another build of the game: one game at a time on the Mac). */
        NSString *(^env_or)(const char *, NSString *) = ^NSString *(const char *name, NSString *fallback) {
            const char *v = getenv(name); return v && *v ? @(v) : fallback; };
        NSString *profile = env_or("TOTK_PROFILE_DIR", [data stringByAppendingPathComponent:@"profile"]);
        NSString *slot = env_or("TOTK_GAME_SLOT_DIR", [data stringByAppendingPathComponent:@"game-slot"]);
        NSString *cache = env_or("TOTK_SHADER_CACHE", [data stringByAppendingPathComponent:[NSString stringWithFormat:@"shader-cache/rt-%s", DMSH_CHAIN]]);
        if (!make_dir(profile) || !make_dir(slot) || !make_dir(cache) || !make_dir(logs)) {
            show_error([NSString stringWithFormat:@"Cannot create %@.", data]);
            return 1;
        }
        pid_t running = running_game(slot);
        if (running) {
            [[NSRunningApplication runningApplicationWithProcessIdentifier:running] activateWithOptions:NSApplicationActivateAllWindows];
            return 0;
        }
#if defined(TOTK_GAME_V112) || defined(TOTK_GAME_V121) || defined(TOTK_GAME_V143)
        NSString *backup = [profile stringByAppendingFormat:@"-before-%s", TOTK_GAME_VERSION];
        NSFileManager *fm = NSFileManager.defaultManager;
        if (![fm fileExistsAtPath:backup]) {
            NSString *temporary = [backup stringByAppendingFormat:@".copy-%d", getpid()];
            NSError *error = nil;
            if (![fm copyItemAtPath:profile toPath:temporary error:&error] ||
                ![fm moveItemAtPath:temporary toPath:backup error:&error]) {
                [fm removeItemAtPath:temporary error:nil];
                show_error([@"Cannot back up saves before upgrading: " stringByAppendingString:error.localizedDescription ?: @"unknown error"]);
                return 1;
            }
        }
#endif
        if (chdir(root.fileSystemRepresentation) != 0) {   // the game reads its data relative to this folder
            show_error(@"Cannot open the game folder inside the app.");
            return 1;
        }
        /* The player's controller layout (runtime/controller_map.h), checked here: a bad line shows this alert instead of
           the game closing at start. */
        NSString *controls = env_or("TOTK_CONTROLS", [data stringByAppendingPathComponent:@"controls.txt"]);
        NSData *layout = [NSData dataWithContentsOfFile:controls];
        if (layout) {
            NSString *text = layout.length < 16384 ? [[NSString alloc] initWithData:layout encoding:NSUTF8StringEncoding] : nil;
            uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controller_map_default(map, modes);
            const char *problem = layout.length < 16384 ? "not UTF-8 text" : "larger than 16 KB";
            int line = text ? controller_map_parse(text.UTF8String, map, modes, &problem) : 1;
            if (line) { show_error([NSString stringWithFormat:@"%@, line %d: %s", controls, line, problem]); return 1; }
        }
        /* The player's settings (runtime/settings_schema.h): what the game reads at start goes to it as its variables; the
           game reloads the file for the settings menu (TOTK_SETTINGS). A variable already set wins. */
        { NSString *settings = env_or("TOTK_SETTINGS", [data stringByAppendingPathComponent:@"settings.txt"]);
          int values[SETTINGS_COUNT]; settings_defaults(values, 0);
          NSData *text = [NSData dataWithContentsOfFile:settings];
          if (text.length && text.length < 16384) settings_parse_text([[NSString alloc] initWithData:text encoding:NSUTF8StringEncoding].UTF8String, values);
          settings_take_env(values); settings_export_env(values);
          setenv("TOTK_SETTINGS", settings.fileSystemRepresentation, 1); }
        setenv("TOTK_CONTROLS", controls.fileSystemRepresentation, 1);
        setenv("TOTK_PROFILE_DIR", profile.fileSystemRepresentation, 1);
        setenv("TOTK_SHADER_CACHE", cache.fileSystemRepresentation, 1);
        setenv("TOTK_GAME_SLOT_DIR", slot.fileSystemRepresentation, 1);
        setenv("TOTK_ROMFS_PROOF_DIR", data.fileSystemRepresentation, 1);   // the app bundle stays read-only
        game_data(root, YES);   // the game data outside the app: its folders (and the RomFS record next to the image)
        setenv("TOTK_SHARED_FONTS", "extracted/firmware/shared-fonts", 1);
        /* Player mods (runtime/mods.h): one folder per mod in <data>/mods, mods.txt for the order; a TOTK_MODS_DIR already
           set wins (a test's own folder). The older single folder <data>/mods/<version> with its manifest.tsv still loads. */
        setenv("TOTK_MODS_DIR", [data stringByAppendingPathComponent:@"mods"].fileSystemRepresentation, 0);
        setenv("TOTK_ROMFS_MODS", [data stringByAppendingPathComponent:@"mods/" TOTK_GAME_DIR].fileSystemRepresentation, 0);
        /* The game's language (runtime/services.c): the app's first preferred language, which also follows a per-app
           language (defaults write org.macnativegames.totk AppleLanguages -array pt-BR). TOTK_LANG still overrides it. */
        NSString *preferred = NSLocale.preferredLanguages.firstObject;
        if (preferred) setenv("TOTK_LOCALE", preferred.UTF8String, 1);
        setenv("TOTK_GPU_ALLOW_SUBMIT", "1", 1);
        setenv("TOTK_GPU_WIDEN", "1", 1);
        setenv("TOTK_GPU_BLOCKRT", "1", 1);
        setenv("TOTK_GPU_MIDCLEAR", "1", 1);
        /* 5000 MB, more with the 4K maximum output (settings: display.headroom 4K, or Auto on 16 GB): a 4K canvas alone runs
           at 4.5-4.9 GB and a size switch holds old and new targets for a moment (stopped at 5023 MB, 2026-10-20). Never
           above the runtime's own default, 40% of the memory. */
        { uint64_t mem = 0; size_t len = sizeof mem; sysctlbyname("hw.memsize", &mem, &len, NULL, 0);
          const char *out = getenv("TOTK_OUTPUT_RES");
          int big = out ? !strncmp(out, "3840", 4) : mem >= (16ULL << 30);
          unsigned budget = big ? 6500 : 5000, cap = (unsigned)((mem / 5 * 2) >> 20);
          if (cap && budget > cap) budget = cap;
          setenv("TOTK_GPU_BUDGET_MB", [NSString stringWithFormat:@"%u", budget].UTF8String, 1); }
        setenv("TOTK_SLOT_OWNER", "user", 0);
        setenv("TOTK_SLOT_PURPOSE", "player", 0);
        NSString *contrib = env_or("TOTK_SHADER_CONTRIB", [data stringByAppendingPathComponent:@"shader-list-contribution.txt"]);
        setenv("TOTK_SHADER_CONTRIB", contrib.fileSystemRepresentation, 1);
        contrib_reminder(contrib);
        unsetenv("TOTK_AUTOLOAD");   // the game starts at its title screen, as on the console
        /* runtime command channel (runtime/input.c): echo "shot ~/Desktop/shot.png" >> "<data>/input.txt" */
        { NSString *cmds = env_or("TOTK_INPUT_FILE", [data stringByAppendingPathComponent:@"input.txt"]);
          FILE *cf = fopen(cmds.fileSystemRepresentation, "w"); if (cf) fclose(cf);
          setenv("TOTK_INPUT_FILE", cmds.fileSystemRepresentation, 1); }

        NSString *log = [logs stringByAppendingPathComponent:@"totk.log"];
        [NSFileManager.defaultManager removeItemAtPath:[logs stringByAppendingPathComponent:@"totk.previous.log"] error:nil];
        [NSFileManager.defaultManager moveItemAtPath:log toPath:[logs stringByAppendingPathComponent:@"totk.previous.log"] error:nil];
        FILE *lf = fopen(log.fileSystemRepresentation, "w");
        if (lf) {
            dup2(fileno(lf), STDOUT_FILENO);
            dup2(fileno(lf), STDERR_FILENO);
            fclose(lf);
        }
        char *const arguments[] = {(char *)binary.fileSystemRepresentation, "rewritten/" TOTK_GAME_DIR, "boot", NULL};
        execv(arguments[0], arguments);
        fprintf(stderr, "[launcher] execv failed: %s\n", strerror(errno));
        show_error([NSString stringWithFormat:@"Starting the game failed. See %@.", log]);
        return 1;
    }
}
