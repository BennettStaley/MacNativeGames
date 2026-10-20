#import "mods_menu.h"
#import "mod_browser.h"
#include "mods.h"

static NSString *const CONFLICT_TEXT = @"Two mods change the same file: the first in the list wins. Merging is coming later";

/* The lines under an installed mod: code, TKCL, conflicts and other versions in the words the player was promised; the
   rest from mods_notice (runtime/mods_panel_mac.h shows the same on the Mac). */
static NSArray<NSString *> *notices_for(const ModInfo *m) {
    static const unsigned order[] = {MOD_HAS_CODE, MOD_TKCL, MOD_CONFLICT, MOD_OLD_VERSION, MOD_OWN_RSTB, MOD_EMPTY};
    NSMutableArray *out = [NSMutableArray array];
    for (size_t i = 0; i < sizeof order / sizeof *order; i++) {
        unsigned f = order[i]; if (!(m->flags & f)) continue;
        const char *s = mods_notice(f); NSString *line = s && *s ? @(s) : @"";
        if (f == MOD_HAS_CODE) line = @"Code patches (exefs) aren't applied yet — coming in a later update";
        if (f == MOD_TKCL) line = @"TKMM .tkcl packages aren't supported yet — coming later";
        if (f == MOD_CONFLICT) line = m->conflicts ? [CONFLICT_TEXT stringByAppendingFormat:@" (%u file%s)", m->conflicts, m->conflicts == 1 ? "" : "s"] : CONFLICT_TEXT;
        if (f == MOD_OLD_VERSION) line = [NSString stringWithFormat:@"Made for %s: may not work on 1.4.3", m->game_versions[0] ? m->game_versions : "an older game version"];
        if (line.length) [out addObject:line];
    }
    return out;
}

NSString *TOTKModsSummary(void) {
    mods_scan();
    int n = mods_count(), on = 0; ModInfo m;
    for (int i = 0; i < n; i++) if (!mods_get(i, &m) && m.enabled) on++;
    return n ? [NSString stringWithFormat:@"%d installed, %d on", n, on] : @"None installed";
}

static NSString *count_text(NSInteger n) {
    return n >= 1000000 ? [NSString stringWithFormat:@"%.1fM", n / 1e6] : n >= 10000 ? [NSString stringWithFormat:@"%ldk", (long)(n / 1000)] :
           n >= 1000 ? [NSString stringWithFormat:@"%.1fk", n / 1e3] : [NSString stringWithFormat:@"%ld", (long)n];
}

/* A picture scaled into a fixed box, so table cells line up while pictures load. */
static UIImage *boxed(UIImage *image, CGSize size) {
    UIGraphicsImageRenderer *r = [[UIGraphicsImageRenderer alloc] initWithSize:size];
    return [r imageWithActions:^(UIGraphicsImageRendererContext *ctx) {
        [[UIColor colorWithWhite:1 alpha:0.06] setFill]; [ctx fillRect:CGRectMake(0, 0, size.width, size.height)];
        if (!image) return;
        CGFloat s = MIN(size.width / image.size.width, size.height / image.size.height);
        CGSize d = CGSizeMake(image.size.width * s, image.size.height * s);
        [image drawInRect:CGRectMake((size.width - d.width) / 2, (size.height - d.height) / 2, d.width, d.height)];
    }];
}

static UILabel *banner_label(void) {
    UILabel *l = [UILabel new];
    l.text = @"Restart the game to apply mod changes"; l.textAlignment = NSTextAlignmentCenter;
    l.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold]; l.textColor = UIColor.blackColor;
    l.backgroundColor = UIColor.systemOrangeColor; l.layer.cornerRadius = 8; l.clipsToBounds = YES;
    l.frame = CGRectMake(0, 0, 320, 36);
    return l;
}

#pragma mark - Installed

@implementation TOTKModsViewController {
    NSMutableArray<NSString *> *folders;
}

- (instancetype)init { return [super initWithStyle:UITableViewStyleInsetGrouped]; }

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Mods";
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(reload) name:TOTKModsChangedNotification object:nil];
    [self reload];
}

- (void)reload {
    mods_scan();
    folders = [NSMutableArray array]; ModInfo m;
    for (int i = 0; i < mods_count(); i++) if (!mods_get(i, &m)) [folders addObject:@(m.folder)];
    self.tableView.tableHeaderView = mods_changed_since_start() ? banner_label() : nil;
    [self.tableView reloadData];
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { (void)tableView; return 2; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView; return section == 0 ? MAX(1, (NSInteger)folders.count) : 1;
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView; return section == 0 ? @"Installed (load order: higher wins)" : @"Get mods";
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView;
    return section == 0 ? @"Changes apply the next time the game starts. Tap Edit to reorder or delete. Mods can also be copied into Files › Tears of the Kingdom › mods (a folder with romfs/ inside)."
                        : TOTKModBrowserFooter;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.detailTextLabel.numberOfLines = 0; cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
    if (path.section == 1) {
        cell.textLabel.text = @"Browse GameBanana"; cell.textLabel.textColor = self.view.tintColor;
        cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
        return cell;
    }
    ModInfo m;
    if (!folders.count || mods_get((int)path.row, &m)) {
        cell.textLabel.text = @"No mods yet"; cell.textLabel.textColor = UIColor.secondaryLabelColor;
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        return cell;
    }
    cell.textLabel.text = @(m.name); cell.textLabel.numberOfLines = 0;
    cell.textLabel.textColor = m.enabled ? UIColor.labelColor : UIColor.secondaryLabelColor;
    NSMutableArray *meta = [NSMutableArray array];
    if (m.author[0]) [meta addObject:[@"by " stringByAppendingString:@(m.author)]];
    if (m.version[0]) [meta addObject:[@"v" stringByAppendingString:@(m.version)]];
    if (m.files) [meta addObject:[NSString stringWithFormat:@"%u file%s, %@", m.files, m.files == 1 ? "" : "s",
                                  [NSByteCountFormatter stringFromByteCount:(long long)m.bytes countStyle:NSByteCountFormatterCountStyleFile]]];
    if (m.game_versions[0]) [meta addObject:[@"for " stringByAppendingString:@(m.game_versions)]];
    NSMutableAttributedString *detail = [[NSMutableAttributedString alloc] initWithString:[meta componentsJoinedByString:@" · "]
        attributes:@{NSForegroundColorAttributeName: UIColor.secondaryLabelColor, NSFontAttributeName: [UIFont systemFontOfSize:13]}];
    for (NSString *line in notices_for(&m))
        [detail appendAttributedString:[[NSAttributedString alloc] initWithString:[@"\n⚠︎ " stringByAppendingString:line]
            attributes:@{NSForegroundColorAttributeName: [line containsString:@"ResourceSizeTable"] ? UIColor.secondaryLabelColor : UIColor.systemOrangeColor,
                         NSFontAttributeName: [UIFont systemFontOfSize:13]}]];
    cell.detailTextLabel.attributedText = detail;
    UISwitch *toggle = [UISwitch new]; toggle.on = m.enabled != 0; toggle.tag = path.row;
    [toggle addTarget:self action:@selector(switched:) forControlEvents:UIControlEventValueChanged];
    cell.accessoryView = toggle; cell.editingAccessoryView = nil; cell.selectionStyle = UITableViewCellSelectionStyleNone;
    return cell;
}

- (void)switched:(UISwitch *)toggle {
    if (toggle.tag >= (NSInteger)folders.count) return;
    mods_set_enabled(folders[toggle.tag].fileSystemRepresentation, toggle.on); mods_save();
    [self reload];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (path.section == 1) [self.navigationController pushViewController:[TOTKModBrowserViewController new] animated:YES];
}

- (BOOL)tableView:(UITableView *)tableView canEditRowAtIndexPath:(NSIndexPath *)path { (void)tableView; return path.section == 0 && folders.count; }
- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)path { (void)tableView; return path.section == 0 && folders.count > 1; }
- (NSIndexPath *)tableView:(UITableView *)tableView targetIndexPathForMoveFromRowAtIndexPath:(NSIndexPath *)from toProposedIndexPath:(NSIndexPath *)to {
    (void)tableView; (void)from;
    return to.section == 0 ? to : [NSIndexPath indexPathForRow:(NSInteger)folders.count - 1 inSection:0];
}
- (void)tableView:(UITableView *)tableView moveRowAtIndexPath:(NSIndexPath *)from toIndexPath:(NSIndexPath *)to {
    (void)tableView;
    mods_move(folders[from.row].fileSystemRepresentation, (int)to.row); mods_save();
    NSString *f = folders[from.row]; [folders removeObjectAtIndex:from.row]; [folders insertObject:f atIndex:to.row];
    dispatch_async(dispatch_get_main_queue(), ^{ [self reload]; });   /* the "#n" and conflict lines follow the new order */
}
- (void)tableView:(UITableView *)tableView commitEditingStyle:(UITableViewCellEditingStyle)style forRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    if (style != UITableViewCellEditingStyleDelete || path.row >= (NSInteger)folders.count) return;
    NSString *folder = folders[path.row]; ModInfo m; mods_get((int)path.row, &m);
    UIAlertController *a = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"Remove “%s”?", m.name]
        message:[NSString stringWithFormat:@"This deletes its folder “%@” from the mods folder.", folder] preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"Remove" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *x) {
        (void)x; mods_remove(folder.fileSystemRepresentation); mods_save(); [self reload]; }]];
    [a addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}
@end

#pragma mark - Browse

@implementation TOTKModBrowserViewController {
    UISearchController *searchController;
    NSMutableArray<TOTKModSummary *> *results;
    NSInteger page, total, generation; BOOL loading; NSString *error;
    TOTKModSort sort;
}

- (instancetype)init { return [super initWithStyle:UITableViewStylePlain]; }

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"GameBanana";
    mods_scan();   /* the "Installed" marks */
    results = [NSMutableArray array];
    searchController = [[UISearchController alloc] initWithSearchResultsController:nil];
    searchController.obscuresBackgroundDuringPresentation = NO; searchController.searchBar.delegate = self;
    searchController.searchBar.placeholder = @"Search Tears of the Kingdom mods";
    self.navigationItem.searchController = searchController; self.navigationItem.hidesSearchBarWhenScrolling = NO;
    self.definesPresentationContext = YES;
    [self buildSortMenu];
    self.tableView.rowHeight = UITableViewAutomaticDimension; self.tableView.estimatedRowHeight = 76;
    [NSNotificationCenter.defaultCenter addObserver:self.tableView selector:@selector(reloadData) name:TOTKModsChangedNotification object:nil];
    [self load:1];
}

- (void)buildSortMenu {
    NSMutableArray *items = [NSMutableArray array]; NSArray *labels = TOTKModSortLabels();
    for (NSUInteger i = 0; i < labels.count; i++) {
        __weak TOTKModBrowserViewController *weak = self;
        UIAction *a = [UIAction actionWithTitle:labels[i] image:nil identifier:nil handler:^(UIAction *x) {
            (void)x; TOTKModBrowserViewController *s = weak; if (!s) return;
            s->sort = (TOTKModSort)i; [s buildSortMenu]; [s load:1]; }];
        a.state = (NSInteger)i == sort ? UIMenuElementStateOn : UIMenuElementStateOff;
        [items addObject:a];
    }
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"arrow.up.arrow.down"]
                                                                               menu:[UIMenu menuWithTitle:@"Sort" children:items]];
}

/* Searches on the Search key, not on every letter (one request per search). */
- (void)searchBarSearchButtonClicked:(UISearchBar *)bar { [bar resignFirstResponder]; [self load:1]; }
- (void)searchBarCancelButtonClicked:(UISearchBar *)bar { (void)bar; dispatch_async(dispatch_get_main_queue(), ^{ [self load:1]; }); }

- (void)load:(NSInteger)n {
    NSInteger gen = ++generation; loading = YES; error = nil;
    if (n == 1) { [results removeAllObjects]; total = 0; [self.tableView reloadData]; }
    [TOTKModBrowser.shared listPage:n sort:sort search:searchController.searchBar.text category:0
                         completion:^(NSArray<TOTKModSummary *> *mods, NSInteger count, NSError *e) {
        if (gen != self->generation) return;
        self->loading = NO; self->error = e.localizedDescription;
        if (!e) { [self->results addObjectsFromArray:mods]; self->page = n; self->total = count; }
        [self.tableView reloadData];
    }];
}

- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView; (void)section; return (NSInteger)results.count + 1;   /* + the status / footer row */
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.detailTextLabel.numberOfLines = 0; cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
    if (path.row >= (NSInteger)results.count) {
        cell.textLabel.text = loading ? @"Loading from GameBanana…" : error ? [@"Couldn't reach GameBanana: " stringByAppendingString:error] :
                              !results.count ? @"No mods found." : (NSInteger)results.count < total ? @"More…" : @"";
        cell.textLabel.textColor = error ? UIColor.systemRedColor : UIColor.secondaryLabelColor; cell.textLabel.numberOfLines = 0;
        cell.detailTextLabel.text = TOTKModBrowserFooter; cell.detailTextLabel.font = [UIFont systemFontOfSize:12];
        cell.selectionStyle = UITableViewCellSelectionStyleNone;
        return cell;
    }
    TOTKModSummary *m = results[path.row];
    cell.textLabel.text = m.name; cell.textLabel.numberOfLines = 2; cell.textLabel.font = [UIFont systemFontOfSize:16 weight:UIFontWeightMedium];
    BOOL have = [TOTKModBrowser.shared installedFolderFor:m.modId] != nil;
    static NSRelativeDateTimeFormatter *f; if (!f) { f = [NSRelativeDateTimeFormatter new]; f.unitsStyle = NSRelativeDateTimeFormatterUnitsStyleShort; }
    cell.detailTextLabel.text = [NSString stringWithFormat:@"%@ · ♥ %@ · %@ views%@%@", m.author, count_text(m.likes), count_text(m.views),
                                 m.updated ? [@" · " stringByAppendingString:[f localizedStringForDate:m.updated relativeToDate:NSDate.date]] : @"",
                                 have ? @"\n✓ Installed" : @""];
    CGSize box = CGSizeMake(96, 54);
    cell.imageView.image = boxed(nil, box); cell.imageView.layer.cornerRadius = 4; cell.imageView.clipsToBounds = YES;
    cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    NSString *url = m.thumbURL; __weak UITableViewCell *weak = cell; cell.accessibilityIdentifier = url;
    if (url) [TOTKModBrowser.shared image:url completion:^(NSData *data) {
        UITableViewCell *c = weak;
        if (data && [c.accessibilityIdentifier isEqual:url]) { c.imageView.image = boxed([UIImage imageWithData:data], box); [c setNeedsLayout]; }
    }];
    return cell;
}

- (void)tableView:(UITableView *)tableView willDisplayCell:(UITableViewCell *)cell forRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView; (void)cell;
    if (path.row >= (NSInteger)results.count - 1 && !loading && !error && (NSInteger)results.count < total && results.count) [self load:page + 1];
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (path.row >= (NSInteger)results.count) { if (error) [self load:results.count ? page + 1 : 1]; return; }
    TOTKModSummary *m = results[path.row];
    [self.navigationController pushViewController:[[TOTKModDetailViewController alloc] initWithModId:m.modId name:m.name] animated:YES];
}
@end

#pragma mark - A mod's page

enum { SEC_TOP, SEC_ABOUT, SEC_LICENSE, SEC_FILES, NSEC };

@implementation TOTKModDetailViewController {
    int64_t modId;
    TOTKModDetails *mod; NSString *error;
    NSMutableDictionary<NSNumber *, NSDictionary *> *state;   /* file id -> fraction, stage, done, error */
    NSMapTable<NSNumber *, UIProgressView *> *bars;
    UIScrollView *gallery;
}

- (instancetype)initWithModId:(int64_t)ident name:(NSString *)name {
    if (!(self = [super initWithStyle:UITableViewStyleInsetGrouped])) return nil;
    modId = ident; self.title = name;
    state = [NSMutableDictionary dictionary]; bars = [NSMapTable strongToWeakObjectsMapTable];
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.navigationItem.largeTitleDisplayMode = UINavigationItemLargeTitleDisplayModeNever;
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithImage:[UIImage systemImageNamed:@"safari"] style:UIBarButtonItemStylePlain
                                                                             target:self action:@selector(openPage)];
    [TOTKModBrowser.shared details:modId completion:^(TOTKModDetails *d, NSError *e) {
        self->mod = d; self->error = e.localizedDescription;
        if (d.name.length) self.title = d.name;
        [self buildGallery]; [self.tableView reloadData];
    }];
}

- (void)openPage { if (mod.profileURL) [UIApplication.sharedApplication openURL:[NSURL URLWithString:mod.profileURL] options:@{} completionHandler:nil]; }

- (void)buildGallery {
    if (!mod.imageURLs.count) { self.tableView.tableHeaderView = nil; return; }
    CGFloat w = MIN(self.tableView.bounds.size.width, 640) - 40, h = round(w * 298 / 530);
    UIView *header = [[UIView alloc] initWithFrame:CGRectMake(0, 0, self.tableView.bounds.size.width, h + 16)];
    gallery = [[UIScrollView alloc] initWithFrame:CGRectMake((header.bounds.size.width - w) / 2, 12, w, h)];
    gallery.pagingEnabled = YES; gallery.showsHorizontalScrollIndicator = YES; gallery.layer.cornerRadius = 10; gallery.clipsToBounds = YES;
    gallery.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin;
    NSUInteger n = MIN(8u, mod.imageURLs.count);
    for (NSUInteger i = 0; i < n; i++) {
        UIImageView *v = [[UIImageView alloc] initWithFrame:CGRectMake(w * i, 0, w, h)];
        v.contentMode = UIViewContentModeScaleAspectFit; v.backgroundColor = [UIColor colorWithWhite:1 alpha:0.05];
        [gallery addSubview:v];
        [TOTKModBrowser.shared image:mod.imageURLs[i] completion:^(NSData *data) { if (data) v.image = [UIImage imageWithData:data]; }];
    }
    gallery.contentSize = CGSizeMake(w * n, h);
    [header addSubview:gallery];
    self.tableView.tableHeaderView = header;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { (void)tableView; return mod ? NSEC : 1; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section {
    (void)tableView;
    if (!mod) return 1;
    return section == SEC_FILES ? MAX(1, (NSInteger)mod.files.count) : 1;
}
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    (void)tableView;
    if (!mod) return nil;
    return section == SEC_ABOUT ? @"About" : section == SEC_LICENSE ? @"License" : section == SEC_FILES ? @"Files" : nil;
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    (void)tableView; return mod && section == SEC_FILES ? TOTKModBrowserFooter : nil;
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    (void)tableView;
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.numberOfLines = 0; cell.detailTextLabel.numberOfLines = 0; cell.detailTextLabel.textColor = UIColor.secondaryLabelColor;
    cell.selectionStyle = UITableViewCellSelectionStyleNone;
    if (!mod) { cell.textLabel.text = error ? [@"Couldn't load the mod's page: " stringByAppendingString:error] : @"Loading…"; return cell; }
    if (path.section == SEC_TOP) {
        cell.textLabel.text = mod.name; cell.textLabel.font = [UIFont systemFontOfSize:20 weight:UIFontWeightBold];
        NSMutableArray *meta = [NSMutableArray array];
        if (mod.author.length) [meta addObject:[@"by " stringByAppendingString:mod.author]];
        if (mod.version.length) [meta addObject:[@"version " stringByAppendingString:mod.version]];
        if (mod.category.length) [meta addObject:mod.category];
        NSString *updated = mod.updated ? [NSDateFormatter localizedStringFromDate:mod.updated dateStyle:NSDateFormatterMediumStyle timeStyle:NSDateFormatterNoStyle] : @"?";
        NSString *installed = [TOTKModBrowser.shared installedFolderFor:mod.modId];
        cell.detailTextLabel.text = [NSString stringWithFormat:@"%@\n%@ downloads · ♥ %@ · %@ views · updated %@%@", [meta componentsJoinedByString:@" · "],
                                     count_text(mod.downloads), count_text(mod.likes), count_text(mod.views), updated,
                                     installed ? [NSString stringWithFormat:@"\n✓ Installed as “%@”", installed] : @""];
        return cell;
    }
    if (path.section == SEC_ABOUT) {
        NSString *text = mod.text.length > 1500 ? [[mod.text substringToIndex:[mod.text rangeOfComposedCharacterSequenceAtIndex:1500].location] stringByAppendingString:@"…"] : mod.text;
        cell.textLabel.text = mod.summary.length ? mod.summary : nil; cell.textLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightMedium];
        cell.detailTextLabel.text = text; cell.detailTextLabel.font = [UIFont systemFontOfSize:14]; cell.detailTextLabel.textColor = UIColor.labelColor;
        return cell;
    }
    if (path.section == SEC_LICENSE) { cell.textLabel.text = mod.license; cell.textLabel.font = [UIFont systemFontOfSize:14]; return cell; }
    if (!mod.files.count) { cell.textLabel.text = @"No files to download."; return cell; }
    TOTKModFile *f = mod.files[path.row]; NSNumber *key = @(f.fileId); NSDictionary *st = state[key];
    BOOL busy = [TOTKModBrowser.shared isInstalling:f.fileId];
    NSMutableAttributedString *title = [[NSMutableAttributedString alloc] initWithString:f.name attributes:@{NSFontAttributeName: [UIFont systemFontOfSize:15 weight:UIFontWeightMedium]}];
    if (f.recommended) [title appendAttributedString:[[NSAttributedString alloc] initWithString:@"  ★ Recommended"
        attributes:@{NSForegroundColorAttributeName: UIColor.systemGreenColor, NSFontAttributeName: [UIFont systemFontOfSize:13 weight:UIFontWeightBold]}]];
    cell.textLabel.attributedText = title;
    NSMutableAttributedString *d = [[NSMutableAttributedString alloc] initWithString:[NSString stringWithFormat:@"%@ · %@ · %@ downloads%@", f.sizeText, f.versionLabel,
        count_text(f.downloads), f.archiveKind.length ? [@" · " stringByAppendingString:f.archiveKind] : @""]
        attributes:@{NSForegroundColorAttributeName: UIColor.secondaryLabelColor, NSFontAttributeName: [UIFont systemFontOfSize:13]}];
    void (^add)(NSString *, UIColor *) = ^(NSString *s, UIColor *c) {
        [d appendAttributedString:[[NSAttributedString alloc] initWithString:[@"\n" stringByAppendingString:s] attributes:@{NSForegroundColorAttributeName: c, NSFontAttributeName: [UIFont systemFontOfSize:13]}]]; };
    if (f.fileDescription.length) add(f.fileDescription, UIColor.labelColor);
    if (f.notice) add([@"⚠︎ " stringByAppendingString:[f.notice stringByReplacingOccurrencesOfString:@"\n" withString:@"\n⚠︎ "]], UIColor.systemOrangeColor);
    if (st) add(st[@"stage"], st[@"error"] ? UIColor.systemRedColor : st[@"done"] ? UIColor.systemGreenColor : UIColor.secondaryLabelColor);
    cell.detailTextLabel.attributedText = d;
    UIButton *act = [UIButton buttonWithType:UIButtonTypeSystem];
    [act setTitle:busy ? @"Cancel" : st[@"done"] ? @"Again" : @"Install" forState:UIControlStateNormal];
    act.titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightSemibold];
    act.enabled = busy || (f.canInstallHere && f.archiveKind.length); act.tag = path.row;
    [act addTarget:self action:busy ? @selector(cancel:) : @selector(install:) forControlEvents:UIControlEventTouchUpInside];
    UIStackView *box = [[UIStackView alloc] initWithArrangedSubviews:@[act]]; box.axis = UILayoutConstraintAxisVertical; box.alignment = UIStackViewAlignmentTrailing;
    box.spacing = 4; box.frame = CGRectMake(0, 0, 90, 44);
    if (busy) {
        UIProgressView *bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
        bar.progress = (float)MAX(0, [st[@"fraction"] doubleValue]); [bar.widthAnchor constraintEqualToConstant:80].active = YES;
        [box addArrangedSubview:bar]; [bars setObject:bar forKey:key];
    }
    cell.accessoryView = box;
    return cell;
}

- (void)reloadFile:(int64_t)fileId {
    for (NSUInteger i = 0; i < mod.files.count; i++)
        if (mod.files[i].fileId == fileId) [self.tableView reloadRowsAtIndexPaths:@[[NSIndexPath indexPathForRow:(NSInteger)i inSection:SEC_FILES]] withRowAnimation:UITableViewRowAnimationNone];
}

- (void)install:(UIButton *)b {
    if (b.tag >= (NSInteger)mod.files.count) return;
    TOTKModFile *f = mod.files[b.tag]; NSNumber *key = @(f.fileId); int64_t fid = f.fileId;
    state[key] = @{@"fraction": @0, @"stage": @"Starting…"};
    __weak TOTKModDetailViewController *weak = self;
    [TOTKModBrowser.shared install:f of:mod progress:^(double fraction, NSString *stage) {
        TOTKModDetailViewController *s = weak; if (!s) return;
        NSString *line = fraction >= 0 ? [NSString stringWithFormat:@"%@ %d%%", stage, (int)lround(fraction * 100)] : stage;
        BOOL stageChanged = ![s->state[key][@"stage"] hasPrefix:stage];
        s->state[key] = @{@"fraction": @(fraction), @"stage": line};
        UIProgressView *bar = [s->bars objectForKey:key];
        if (bar && fraction >= 0) bar.progress = (float)fraction;
        if (stageChanged) [s reloadFile:fid];
    } completion:^(NSString *folder, NSString *note, NSError *e) {
        TOTKModDetailViewController *s = weak; if (!s) return;
        NSString *line = folder ? [NSString stringWithFormat:@"Installed as “%@” and switched on.%@%@", folder, note ? @"\n" : @"", note ?: @""] : e.localizedDescription;
        s->state[key] = folder ? @{@"fraction": @1, @"stage": line, @"done": @YES} : @{@"fraction": @0, @"stage": line, @"error": @YES};
        [s.tableView reloadData];
    }];
    [self reloadFile:fid];
}

- (void)cancel:(UIButton *)b { if (b.tag < (NSInteger)mod.files.count) [TOTKModBrowser.shared cancelInstall:mod.files[b.tag].fileId]; }
@end
