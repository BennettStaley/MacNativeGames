#import <UIKit/UIKit.h>
#import <AVFAudio/AVFAudio.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include "runtime.h"
#import "touch_pad.h"
#import "settings_menu.h"
#import "saves_menu.h"
#include "controller_map.h"
#include "settings.h"

int totk_host_main(int argc, char **argv);
void runtime_ios_attach_layer(CAMetalLayer *layer, UIView *host);
static char *host_argv[3];
static void *run_host(void *unused) { (void)unused; return (void *)(intptr_t)totk_host_main(3, host_argv); }
/* The desktop keyboard-overlay shortcut has no counterpart on iOS. */
void runtime_shortcut_help_toggle(void) {}

@interface TOTKMetalView : UIView @end
@implementation TOTKMetalView
+ (Class)layerClass { return CAMetalLayer.class; }
@end

/* Settings changes made outside the menu (none yet) reach the app's views the same way as the menu's own. */
static void settings_changed(const char *key, int value) {
    (void)value; NSString *k = @(key);
    dispatch_async(dispatch_get_main_queue(), ^{ if (!TOTKSettingsMenu.isOpen) [NSNotificationCenter.defaultCenter postNotificationName:TOTKSettingsChangedNotification object:nil userInfo:@{@"key": k}]; });
}

/* The settings menu opens with three fingers, the gear at the top, a controller's Home button, − and + held together
   for half a second, or Esc / Command-comma on a keyboard. */
@interface TOTKGameController : UIViewController @property(nonatomic,strong) TOTKMetalView *gameView; @end
@implementation TOTKGameController {
    UIButton *gear; CADisplayLink *link; CFTimeInterval chordSince; BOOL homeWas;
}
- (void)openSettings { [TOTKSettingsMenu openFrom:self]; }
- (void)applySettings { gear.hidden = !settings_get("interface.menu_button"); gear.alpha = MAX(.25, settings_get("interface.touch_opacity") / 100.0); }
- (void)poll:(CADisplayLink *)sender {
    if (TOTKSettingsMenu.isOpen) { chordSince = 0; homeWas = YES; return; }
    BOOL chord = NO, home = NO;
    for (GCController *controller in GCController.controllers) {
        GCExtendedGamepad *pad = controller.extendedGamepad; if (!pad) continue;
        if (pad.buttonOptions.pressed && pad.buttonMenu.pressed) chord = YES;
        if (pad.buttonHome.pressed) home = YES;
    }
    if (home && !homeWas) [self openSettings];
    homeWas = home;
    if (!chord) chordSince = 0;
    else if (!chordSince) chordSince = sender.timestamp;
    else if (sender.timestamp - chordSince > .5) { chordSince = 0; [self openSettings]; }
}
- (void)claimHome:(NSNotification *)note {   /* ask iOS to pass the Home button to the game instead of its own menu */
    (void)note;
    for (GCController *controller in GCController.controllers) controller.extendedGamepad.buttonHome.preferredSystemGestureState = GCSystemGestureStateDisabled;
}
- (BOOL)canBecomeFirstResponder { return YES; }
- (NSArray<UIKeyCommand *> *)keyCommands {
    return @[[UIKeyCommand keyCommandWithInput:UIKeyInputEscape modifierFlags:0 action:@selector(openSettings)],
             [UIKeyCommand commandWithTitle:@"Settings" image:nil action:@selector(openSettings) input:@"," modifierFlags:UIKeyModifierCommand propertyList:nil]];
}
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.gameView = [[TOTKMetalView alloc] initWithFrame:self.view.bounds];
    self.gameView.autoresizingMask = UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.gameView];
    runtime_ios_attach_layer((CAMetalLayer *)self.gameView.layer, self.gameView);
    TOTKTouchPad *pad=[[TOTKTouchPad alloc] initWithFrame:self.view.bounds];pad.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;[self.view addSubview:pad];
    UITapGestureRecognizer *three = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(openSettings)];
    three.numberOfTouchesRequired = 3; [self.view addGestureRecognizer:three];
    gear = [UIButton buttonWithType:UIButtonTypeSystem];
    [gear setImage:[UIImage systemImageNamed:@"gearshape.fill" withConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:18]] forState:UIControlStateNormal];
    gear.tintColor = UIColor.whiteColor; gear.translatesAutoresizingMaskIntoConstraints = NO;
    [gear addTarget:self action:@selector(openSettings) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:gear];
    [NSLayoutConstraint activateConstraints:@[[gear.centerXAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.centerXAnchor],
        [gear.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:4],
        [gear.widthAnchor constraintEqualToConstant:44], [gear.heightAnchor constraintEqualToConstant:44]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(applySettings) name:TOTKSettingsChangedNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(claimHome:) name:GCControllerDidConnectNotification object:nil];
    [self claimHome:nil]; [self applySettings];
    link = [CADisplayLink displayLinkWithTarget:self selector:@selector(poll:)];
    link.preferredFrameRateRange = CAFrameRateRangeMake(20, 30, 30);   /* a controller poll, not a picture */
    [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    [self becomeFirstResponder];
    static BOOL started; if (started) return; started = YES;
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t thread; pthread_create(&thread, &attr, run_host, NULL);
    pthread_detach(thread); pthread_attr_destroy(&attr);
}
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

@interface TOTKApp : UIResponder <UIApplicationDelegate> @property(nonatomic,strong) UIWindow *window; @end
@implementation TOTKApp
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)options;
    // Controller input does not reset the idle timer; auto-lock would suspend the game mid-play or mid shader import.
    application.idleTimerDisabled = YES;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"];
    for (NSString *name in @[@"save", @"temp", @"shader-cache", @"game-slot", @"mods"]) [fm createDirectoryAtPath:[docs stringByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *log = [docs stringByAppendingPathComponent:@"log.txt"];
    if (freopen(log.fileSystemRepresentation,"w",stderr)) setvbuf(stderr,NULL,_IOLBF,0);
    setenv("TOTK_ROMFS",[res stringByAppendingPathComponent:@"romfs"].fileSystemRepresentation,1);
    setenv("TOTK_OPERATION_MODE","handheld",1);
    setenv("TOTK_ROMFS_PATCHES",[res stringByAppendingPathComponent:@"romfs-patches"].fileSystemRepresentation,1);
    setenv("TOTK_ROMFS_PATCHES_OUT",[res stringByAppendingPathComponent:@"romfs-patches-out"].fileSystemRepresentation,1);
    setenv("TOTK_SHARED_FONTS",[res stringByAppendingPathComponent:@"shared-fonts"].fileSystemRepresentation,1);
    setenv("TOTK_SHADER_SEED_DIR",[res stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation,1);
    setenv("TOTK_SHADER_CACHE",[docs stringByAppendingPathComponent:@"shader-cache"].fileSystemRepresentation,1);
    // Shaders the shared list lacks (totk_shader_contrib.h), in the Files app for players who want to send them; only the Mac asks.
    setenv("TOTK_SHADER_CONTRIB",[docs stringByAppendingPathComponent:@"shader-list-contribution.txt"].fileSystemRepresentation,1);
    setenv("DM_NVN_SHMAP",[res stringByAppendingPathComponent:@"shader-seed/shader_map.tsv"].fileSystemRepresentation,1);
    setenv("DM_MSL_DIR",[res stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation,1);
    NSString *font = [res stringByAppendingPathComponent:@"font"]; setenv("TOTK_FONT_DIR",font.fileSystemRepresentation,1);
    // Same player environment as the Mac launcher (app/launcher.m); the bundle is read-only, so state goes to Documents.
    setenv("TOTK_PROFILE_DIR",[docs stringByAppendingPathComponent:@"save"].fileSystemRepresentation,1);
    setenv("TOTK_GAME_SLOT_DIR",[docs stringByAppendingPathComponent:@"game-slot"].fileSystemRepresentation,1);
    setenv("TOTK_ROMFS_PROOF_DIR",docs.fileSystemRepresentation,1);   // hash the 17 GB RomFS once, not on every launch
    // Player mods (runtime/mods.h): one folder per mod in Documents/mods, which the Files app shows (UIFileSharingEnabled).
    setenv("TOTK_MODS_DIR",[docs stringByAppendingPathComponent:@"mods"].fileSystemRepresentation,1);
    for (NSString *name in @[@"TOTK_GPU_ALLOW_SUBMIT",@"TOTK_GPU_WIDEN",@"TOTK_GPU_BLOCKRT",@"TOTK_GPU_MIDCLEAR"]) setenv(name.UTF8String,"1",1);
    setenv("TOTK_SLOT_OWNER","user",0); setenv("TOTK_SLOT_PURPOSE","player",0);
    unsetenv("TOTK_AUTOLOAD");
    /* The player's controller layout (runtime/controller_map.h) from Files, as on the Mac; a bad file is left out (and
       logged) rather than closing the game at start. */
    NSString *controls = [docs stringByAppendingPathComponent:@"controls.txt"];
    NSData *layout = [NSData dataWithContentsOfFile:controls];
    if (layout.length && layout.length < 16384) {
        uint16_t map[CONTROLLER_BUTTONS]; uint8_t modes[CONTROLLER_BUTTONS]; controller_map_default(map, modes); const char *problem = "not UTF-8 text";
        NSString *text = [[NSString alloc] initWithData:layout encoding:NSUTF8StringEncoding];
        int line = text ? controller_map_parse(text.UTF8String, map, modes, &problem) : 1;
        if (line) fprintf(stderr, "[controls] %s line %d: %s; using the default layout\n", controls.fileSystemRepresentation, line, problem);
        else setenv("TOTK_CONTROLS", controls.fileSystemRepresentation, 1);
    }
    /* The player's settings (runtime/settings.c): the app is its own launcher, so the start-time ones become variables here. */
    settings_init([docs stringByAppendingPathComponent:@"settings.txt"].fileSystemRepresentation);
    settings_observe(settings_changed);
    chdir(docs.fileSystemRepresentation);
    host_argv[0] = strdup("TOTK"); host_argv[1] = strdup(res.fileSystemRepresentation); host_argv[2] = strdup("boot");
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [TOTKGameController new]; [self.window makeKeyAndVisible]; return YES;
}
/* A file the app is asked to open (AirDrop, Files, Messages; also at a start the file caused): a save package (.totksave)
   goes to saves_menu.m, which checks it and asks before it replaces anything. */
- (BOOL)application:(UIApplication *)application openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {
    (void)application; (void)options;
    return [TOTKSaveReceiver openURL:url];
}
@end

int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(TOTKApp.class)); } }
