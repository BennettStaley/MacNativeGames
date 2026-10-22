// A command line around games/totk/runtime/save_package.m for tests/test_save_package.py (fake profiles only):
//   save_package_tool export <profile> <dir> <game version> <platform> <device>   -> the package's path
//   save_package_tool inspect <package> <game version>                             -> what it holds, as JSON
//   save_package_tool import <package> <profile> <backups> <game version>          -> what was done, as JSON
// A refusal prints "refused: <reason>" and exits with 2.
#import "save_package.h"
#include <stdio.h>
#include <string.h>

static int refuse(NSError *e) { printf("refused: %s\n", e.localizedDescription.UTF8String); return 2; }
static void print_json(NSDictionary *d) {
    NSData *j = [NSJSONSerialization dataWithJSONObject:d options:NSJSONWritingSortedKeys error:nil];
    printf("%s\n", [[NSString alloc] initWithData:j encoding:NSUTF8StringEncoding].UTF8String);
}
static NSDictionary *describe(TOTKSavePackageInfo *i) {
    return @{@"slots": i.slots, @"from": i.fromText, @"from_device": i.fromDevice ?: @"", @"game_version": i.gameVersion,
             @"files": @(i.fileCount), @"bytes": @(i.bytes), @"backup": i.backup ?: @"", @"slots_text": i.slotsText,
             @"created": @(i.created.timeIntervalSince1970)};
}

int main(int argc, char **argv) {
    @autoreleasepool {
        NSError *e = nil;
        if (argc == 7 && !strcmp(argv[1], "export")) {
            NSString *path = TOTKSaveExport(@(argv[2]), @(argv[3]), @(argv[4]), @(argv[5]), @(argv[6]), @"TestModel1,1", &e);
            if (!path) return refuse(e);
            printf("%s\n", path.UTF8String);
            return 0;
        }
        if (argc == 4 && !strcmp(argv[1], "inspect")) {
            TOTKSavePackageInfo *i = TOTKSaveInspect(@(argv[2]), @(argv[3]), &e);
            if (!i) return refuse(e);
            print_json(describe(i));
            return 0;
        }
        if (argc == 6 && !strcmp(argv[1], "import")) {
            TOTKSavePackageInfo *i = TOTKSaveImport(@(argv[2]), @(argv[3]), @(argv[4]), @(argv[5]), &e);
            if (!i) return refuse(e);
            NSMutableDictionary *d = [describe(i) mutableCopy];
            d[@"summary"] = TOTKSaveSummary(@(argv[3]));
            NSDate *when = nil; d[@"last_backup"] = TOTKSaveLastBackup(@(argv[4]), &when) ?: @""; d[@"last_backup_time"] = @(when.timeIntervalSince1970);
            print_json(d);
            return 0;
        }
        fprintf(stderr, "usage: see the top of tests/save_package_tool.m\n");
        return 1;
    }
}
