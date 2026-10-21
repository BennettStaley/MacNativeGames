#import "saves_menu.h"
#import "save_package.h"
#include <sys/utsname.h>
#include <unistd.h>
#include "runtime.h"
#include "game_version.h"

static NSString *documents(void) { return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject; }
static NSString *profile_dir(void) { return [documents() stringByAppendingPathComponent:@"save"]; }          /* TOTK_PROFILE_DIR (main.m) */
static NSString *backups_dir(void) { return [documents() stringByAppendingPathComponent:@"save-backups"]; }
static NSString *device_word(void) { return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPad ? @"iPad" : @"iPhone"; }
static BOOL saving(void) { return runtime_fs_save_busy() || TOTKSaveBusy(profile_dir()); }
static NSString *when_text(NSDate *d) {
    return d ? [NSDateFormatter localizedStringFromDate:d dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterShortStyle] : @"an unknown time";
}

NSString *TOTKSavesSummary(void) { return TOTKSaveSummary(profile_dir()); }

/* The view controller on top, for alerts that arrive from outside the menu (a file opened with the app). */
static UIViewController *top_controller(void) {
    id<UIApplicationDelegate> app = UIApplication.sharedApplication.delegate;
    UIViewController *top = [app respondsToSelector:@selector(window)] ? app.window.rootViewController : nil;
    while (top.presentedViewController && !top.presentedViewController.isBeingDismissed) top = top.presentedViewController;
    return top.viewIfLoaded.window ? top : nil;
}
static void show_when_ready(UIAlertController *alert, int tries) {   /* at a cold start the game's view may not be on screen yet */
    UIViewController *top = top_controller();
    if (top) { [top presentViewController:alert animated:YES completion:nil]; return; }
    if (tries < 40) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ show_when_ready(alert, tries + 1); });
}
static void show(UIAlertController *alert) { dispatch_async(dispatch_get_main_queue(), ^{ show_when_ready(alert, 0); }); }
static UIAlertController *message(NSString *title, NSString *text) {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:title message:text preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleCancel handler:nil]];
    return a;
}

/* Closes the app once no save is being written (the received saves are read at the next start). */
static void close_game(void) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        for (int i = 0; i < 750 && saving(); i++) usleep(20000);
        fprintf(stderr, "[saves] closing the game for the received saves\n");
        exit(0);
    });
}

#pragma mark - Receiving

@implementation TOTKSaveReceiver

/* A copy in the app's own temporary folder: an in-place file from Files is security scoped, AirDrop's sits in
   Documents/Inbox (removed once copied, so it does not show in Files). */
static NSString *copy_in(NSURL *url, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"totk-saves-in"];
    [fm removeItemAtPath:dir error:nil];
    [fm createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    NSURL *dst = [NSURL fileURLWithPath:[dir stringByAppendingPathComponent:url.lastPathComponent]];
    BOOL scoped = [url startAccessingSecurityScopedResource];
    __block BOOL ok = NO; __block NSError *copyError = nil; NSError *coordinated = nil;
    [[NSFileCoordinator new] coordinateReadingItemAtURL:url options:NSFileCoordinatorReadingWithoutChanges error:&coordinated byAccessor:^(NSURL *src) {
        NSError *e = nil; ok = [fm copyItemAtURL:src toURL:dst error:&e]; copyError = e; }];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (ok && [url.path containsString:@"/Documents/Inbox/"]) [fm removeItemAtURL:url error:nil];
    if (!ok && error) *error = copyError ?: coordinated;
    return ok ? dst.path : nil;
}

+ (BOOL)openURL:(NSURL *)url {
    if (![url.pathExtension.lowercaseString isEqual:TOTKSavePackageExtension]) return NO;
    NSError *error = nil;
    NSString *package = copy_in(url, &error);
    fprintf(stderr, "[saves] opened %s: %s\n", url.lastPathComponent.UTF8String, package ? "copied in" : error.localizedDescription.UTF8String);
    if (!package) {
        show(message(@"These saves can't be opened", error.localizedDescription ?: @"The file could not be read."));
        return YES;
    }
    TOTKSavePackageInfo *info = TOTKSaveInspect(package, @TOTK_GAME_VERSION, &error);
    if (!info) {
        show(message(@"These saves can't be used", [NSString stringWithFormat:@"%@\n\nThe saves on this %@ are unchanged.",
                                                    error.localizedDescription, device_word()]));
        return YES;
    }
    UIAlertController *ask = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Use the saves from %@?", info.fromText]
        message:[NSString stringWithFormat:@"Sent %@ (game version %@).\n\n%@\n\nThey replace the saves on this %@. The current saves are kept "
                 @"in Files › Tears of the Kingdom › save-backups (the last %d).", when_text(info.created), info.gameVersion, info.slotsText,
                 device_word(), TOTK_SAVE_BACKUPS_KEPT]
        preferredStyle:UIAlertControllerStyleAlert];
    [ask addAction:[UIAlertAction actionWithTitle:@"Replace Saves" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *a) {
        (void)a; [self import:package tries:0]; }]];
    [ask addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    show(ask);
    return YES;
}

/* The game runs in this process: the files are only replaced while it writes no save (fs_host.c), waiting up to 20 s. */
+ (void)import:(NSString *)package tries:(int)tries {
    if (saving()) {
        if (tries < 40) { dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{ [self import:package tries:tries + 1]; }); return; }
        show(message(@"The game kept saving", @"Nothing was changed. Open the file again in a moment."));
        return;
    }
    NSError *error = nil;
    TOTKSavePackageInfo *info = TOTKSaveImport(package, profile_dir(), backups_dir(), @TOTK_GAME_VERSION, &error);
    fprintf(stderr, "[saves] import: %s\n", info ? "done" : error.localizedDescription.UTF8String);
    [NSFileManager.defaultManager removeItemAtPath:package error:nil];
    if (!info) { show(message(@"These saves can't be used", error.localizedDescription)); return; }
    UIAlertController *done = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Saves from %@ are on this %@", info.fromText, device_word()]
        message:[NSString stringWithFormat:@"%@\n\nThe game still has the old save loaded and would save over the new ones: close it now, open "
                 @"it again from the Home Screen and load a save from the title screen.%@", info.slotsText,
                 info.backup ? @"\n\nThe saves they replaced are in Files › Tears of the Kingdom › save-backups." : @""]
        preferredStyle:UIAlertControllerStyleAlert];
    [done addAction:[UIAlertAction actionWithTitle:@"Close the Game" style:UIAlertActionStyleDefault handler:^(UIAlertAction *a) { (void)a; close_game(); }]];
    [done addAction:[UIAlertAction actionWithTitle:@"Later" style:UIAlertActionStyleCancel handler:nil]];
    show(done);
}
@end

#pragma mark - The Saves page

@implementation TOTKSavesViewController {
    NSIndexPath *sendRow;
}

- (instancetype)init { return [super initWithStyle:UITableViewStyleInsetGrouped]; }
- (void)viewDidLoad { [super viewDidLoad]; self.title = @"Saves"; sendRow = [NSIndexPath indexPathForRow:0 inSection:0]; }
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self.tableView reloadData]; }

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { (void)tableView; return 2; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { (void)tableView; return section ? 2 : 1; }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section { (void)tableView; return section ? @"Receive" : @"Send"; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;
    if (!section)
        return [NSString stringWithFormat:@"%@. Save in the game first: the newest saves on this %@ are what is sent, as one .totksave file. "
                @"AirDrop shows first; Messages, Mail and Save to Files work too.", TOTKSavesSummary(), device_word()];
    return [NSString stringWithFormat:@"Open a .totksave from the Mac or another iPhone / iPad (AirDrop, Files, Messages) to bring its saves "
            @"in. It must come from the same game version (%s). The saves it replaces are kept in Files › Tears of the Kingdom › "
            @"save-backups (the last %d); to go back, copy a backup's contents into save › save › the folder there while the game "
            @"is closed.", TOTK_GAME_VERSION, TOTK_SAVE_BACKUPS_KEPT];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleValue1 reuseIdentifier:nil];
    if ([path isEqual:sendRow]) {
        cell.textLabel.text = @"Send Saves…"; cell.textLabel.textColor = self.view.tintColor;
        cell.imageView.image = [UIImage systemImageNamed:@"square.and.arrow.up"];
        return cell;
    }
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (path.row == 0) {
        NSDate *when = nil; NSString *last = TOTKSaveLastBackup(backups_dir(), &when);
        cell.textLabel.text = @"Last backup"; cell.detailTextLabel.text = last ? when_text(when) : @"None yet";
        cell.imageView.image = [UIImage systemImageNamed:@"clock.arrow.circlepath"];
    } else {
        cell.textLabel.text = @"In Files"; cell.detailTextLabel.text = @"Tears of the Kingdom › save-backups";
        cell.imageView.image = [UIImage systemImageNamed:@"folder"];
    }
    return cell;
}

- (NSIndexPath *)tableView:(UITableView *)tableView willSelectRowAtIndexPath:(NSIndexPath *)path { (void)tableView; return [path isEqual:sendRow] ? path : nil; }
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if ([path isEqual:sendRow]) [self send];
}

- (void)send {
    if (saving()) { [self presentViewController:message(@"The game is saving", @"Try again in a moment.") animated:YES completion:nil]; return; }
    struct utsname u; uname(&u);
    NSError *error = nil;
    NSString *path = TOTKSaveExport(profile_dir(), [NSTemporaryDirectory() stringByAppendingPathComponent:@"totk-saves-out"], @TOTK_GAME_VERSION,
                                    @"ios", UIDevice.currentDevice.name, @(u.machine), &error);
    fprintf(stderr, "[saves] send: %s\n", path ? path.fileSystemRepresentation : error.localizedDescription.UTF8String);
    if (!path) { [self presentViewController:message(@"The saves can't be sent", error.localizedDescription) animated:YES completion:nil]; return; }
    UIActivityViewController *share = [[UIActivityViewController alloc] initWithActivityItems:@[[NSURL fileURLWithPath:path]] applicationActivities:nil];
    UITableViewCell *cell = [self.tableView cellForRowAtIndexPath:sendRow];
    share.popoverPresentationController.sourceView = cell ?: self.view;   /* iPad: the sheet points at the row */
    share.popoverPresentationController.sourceRect = cell ? cell.bounds : self.view.bounds;
    [self presentViewController:share animated:YES completion:nil];
}
@end
