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
#include "dmsh_chain.h"
#import "touch_pad.h"
#import "settings_menu.h"
#import "saves_menu.h"
#import "power.h"
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
    /* Playback category (PR #97): plays with the ring / silent switch off and routes to the speaker. Without an active
       session the game's CoreAudio output stays on a null sink on iOS 27 ("no device sink in use"); activating it in
       didFinishLaunching blocked the main thread before the log file was opened. Interruptions: audio_observe. */
    AVAudioSession *audio = AVAudioSession.sharedInstance;
    NSError *catErr = nil, *actErr = nil;
    [audio setCategory:AVAudioSessionCategoryPlayback mode:AVAudioSessionModeDefault options:0 error:&catErr];
    BOOL activated = [audio setActive:YES error:&actErr];
    fprintf(stderr, "[totk-audio] audio session category=%s active=%d route=%s err=%s%s\n",
            audio.category.UTF8String ?: "?", (int)activated, audio.currentRoute.outputs.firstObject.portType.UTF8String ?: "?",
            catErr ? catErr.localizedDescription.UTF8String : "none", actErr ? actErr.localizedDescription.UTF8String : "");
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t thread; pthread_create(&thread, &attr, run_host, NULL);
    pthread_detach(thread); pthread_attr_destroy(&attr);
}
- (BOOL)prefersHomeIndicatorAutoHidden { return YES; }
- (BOOL)prefersStatusBarHidden { return YES; }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
@end

/* iOS 27 ends apps linked against its SDK that keep the window in the app delegate (UIKit traps in
   _UIApplicationEvaluateRuntimeIssueForNoSceneLifecycleAdoption at launch), so the window belongs to a window scene. */
@interface TOTKSceneDelegate : UIResponder <UIWindowSceneDelegate> @property(nonatomic,strong) UIWindow *window; @end
@implementation TOTKSceneDelegate
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)session; (void)options;
    if (![scene isKindOfClass:UIWindowScene.class]) return;
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [TOTKGameController new]; [self.window makeKeyAndVisible];
}
@end

/* Game audio after interruptions (from nxnative b301431). A call, Siri or an alarm takes the audio session and the
   system stops the game's output unit; a media services reset invalidates it. Nothing started it again, so the game
   stayed silent. When an interruption ends the session is made active again and the output started
   (runtime_audio_output_restart); a reset rebuilds the output. Becoming active does the same, for an interruption
   whose "ended" never came. Becoming active also turns the idle timer off again. */
static BOOL g_audio_interrupted;
static void audio_resume(int recreate, const char *why) {
    NSError *err = nil;
    if (![AVAudioSession.sharedInstance setActive:YES error:&err]) fprintf(stderr,"[totk-audio] audio session not active again (%s): %s\n",why,err.localizedDescription.UTF8String);
    g_audio_interrupted = NO; runtime_audio_output_restart(recreate);
}
static void audio_observe(void) {
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter; NSOperationQueue *main = NSOperationQueue.mainQueue;
    [nc addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:main usingBlock:^(NSNotification *n) {
        NSUInteger type = [n.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
        fprintf(stderr,"[totk-audio] audio session interruption %s\n",type == AVAudioSessionInterruptionTypeBegan ? "began" : "ended");
        if (type == AVAudioSessionInterruptionTypeBegan) g_audio_interrupted = YES; else audio_resume(0,"interruption ended");
    }];
    [nc addObserverForName:AVAudioSessionMediaServicesWereResetNotification object:nil queue:main usingBlock:^(NSNotification *n) {
        (void)n; fprintf(stderr,"[totk-audio] media services were reset\n"); audio_resume(1,"media services reset");
    }];
    [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:main usingBlock:^(NSNotification *n) {
        (void)n; UIApplication.sharedApplication.idleTimerDisabled = YES;
        if (g_audio_interrupted) audio_resume(0,"became active during an interruption");
    }];
}

@interface TOTKApp : UIResponder <UIApplicationDelegate> @end
@implementation TOTKApp
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    (void)application; (void)options;
    UISceneConfiguration *c = [[UISceneConfiguration alloc] initWithName:@"Default" sessionRole:session.role];
    c.delegateClass = TOTKSceneDelegate.class; return c;
}
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)options;
    // Controller input does not reset the idle timer; auto-lock would suspend the game mid-play or mid shader import.
    application.idleTimerDisabled = YES;
    audio_observe();
    /* The Mac launcher passes the preferred language as TOTK_LOCALE (PR #97); "zh-Hans-CN" maps to zh-Hans in services.c. */
    NSString *preferred = NSLocale.preferredLanguages.firstObject;
    if (preferred.length) setenv("TOTK_LOCALE", preferred.UTF8String, 1);
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *docs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    NSString *res = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"GameData"];
    for (NSString *name in @[@"save", @"temp", @"shader-cache", @"game-slot", @"mods"]) [fm createDirectoryAtPath:[docs stringByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:nil];
    NSString *log = [docs stringByAppendingPathComponent:@"log.txt"];
    if (freopen(log.fileSystemRepresentation,"w",stderr)) setvbuf(stderr,NULL,_IOLBF,0);
    power_memory_report();   // memory left to the app, and whether the profile granted the memory entitlements (power.m)
    setenv("TOTK_ROMFS",[res stringByAppendingPathComponent:@"romfs"].fileSystemRepresentation,1);
    setenv("TOTK_OPERATION_MODE","handheld",1);
    // Game language: the Settings app (Settings.bundle › TOTK_LANG) stores the choice in our defaults; empty means the
    // system language, which is what the runtime already follows. iOS 27 does not honour Info.plist LSEnvironment, so
    // the value must be applied here. overwrite=0 keeps devicectl --environment-variables winning when testing from the Mac.
    NSString *lang = [NSUserDefaults.standardUserDefaults stringForKey:@"TOTK_LANG"];
    if (lang.length>0&&lang.length<8) setenv("TOTK_LANG",lang.UTF8String,0);
    setenv("TOTK_ROMFS_PATCHES",[res stringByAppendingPathComponent:@"romfs-patches"].fileSystemRepresentation,1);
    setenv("TOTK_ROMFS_PATCHES_OUT",[res stringByAppendingPathComponent:@"romfs-patches-out"].fileSystemRepresentation,1);
    setenv("TOTK_SHARED_FONTS",[res stringByAppendingPathComponent:@"shared-fonts"].fileSystemRepresentation,1);
    setenv("TOTK_SHADER_SEED_DIR",[res stringByAppendingPathComponent:@"shader-seed"].fileSystemRepresentation,1);
    /* One folder per translation chain, as on the Mac: the runtime refuses translations of another chain, and the seed
       import keeps files that are already there. Builds before the v5 chain (2026-10-20) kept them directly in
       shader-cache; those, and other chains' folders, are only cache and are removed in the background. */
    NSString *cacheRoot = [docs stringByAppendingPathComponent:@"shader-cache"], *chainDir = @"rt-" DMSH_CHAIN;
    NSString *cache = [cacheRoot stringByAppendingPathComponent:chainDir];
    /* Test launches (devicectl --environment-variables '{"TOTK_SHADER_CACHE":"cache-test/1"}') can name another folder
       in Documents, e.g. an empty one to time a first start; it lies outside shader-cache, so the cleanup leaves it. */
    const char *testCache = getenv("TOTK_SHADER_CACHE");
    if (testCache && *testCache && !strstr(testCache, "..")) cache = [docs stringByAppendingPathComponent:@(testCache)];
    [fm createDirectoryAtPath:cache withIntermediateDirectories:YES attributes:nil error:nil];
    setenv("TOTK_SHADER_CACHE",cache.fileSystemRepresentation,1);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{
        NSFileManager *files = [NSFileManager new]; NSUInteger removed = 0;
        for (NSString *name in [files contentsOfDirectoryAtPath:cacheRoot error:nil] ?: @[]) {
            if ([name isEqualToString:chainDir]) continue;
            if ([files removeItemAtPath:[cacheRoot stringByAppendingPathComponent:name] error:nil]) removed++;
        }
        if (removed) fprintf(stderr,"[shader-cache] removed %lu entries of earlier translation chains\n",(unsigned long)removed);
    });
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
    power_observe();   // heat and Low Power Mode hold the frame rate at 30 (power.m)
    chdir(docs.fileSystemRepresentation);
    host_argv[0] = strdup("TOTK"); host_argv[1] = strdup(res.fileSystemRepresentation); host_argv[2] = strdup("boot");
    return YES;   // the window is made when the window scene connects (TOTKSceneDelegate)
}
/* A file the app is asked to open (AirDrop, Files, Messages; also at a start the file caused): a save package (.totksave)
   goes to saves_menu.m, which checks it and asks before it replaces anything. */
- (BOOL)application:(UIApplication *)application openURL:(NSURL *)url options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {
    (void)application; (void)options;
    return [TOTKSaveReceiver openURL:url];
}
@end

int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(TOTKApp.class)); } }
