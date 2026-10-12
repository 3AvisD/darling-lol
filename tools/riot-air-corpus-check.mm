// Feed shader libraries extracted from the game's shader cache to Darling's Metal and report why
// translation fails. usage: riot-air-corpus-check <directory of .bin metallibs> [max files]
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <dirent.h>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

int main(int argc, char** argv) {
    @autoreleasepool {
        if (argc < 2) { fprintf(stderr, "usage: %s <dir> [max]\n", argv[0]); return 2; }
        size_t maxFiles = argc > 2 ? strtoul(argv[2], nullptr, 10) : SIZE_MAX;
        std::vector<std::string> files;
        if (DIR* dir = opendir(argv[1])) {
            while (struct dirent* e = readdir(dir)) {
                std::string n = e->d_name;
                if (n.size() > 4 && n.substr(n.size() - 4) == ".bin") files.push_back(n);
            }
            closedir(dir);
        }
        std::sort(files.begin(), files.end());
        if (files.size() > maxFiles) files.resize(maxFiles);

        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) { fprintf(stderr, "no Metal device\n"); return 1; }

        std::map<std::string, std::pair<int, std::string>> reasons;
        size_t ok = 0, failed = 0;
        for (const std::string& name : files) {
            fprintf(stderr, "CHECK: %s\n", name.c_str());
            NSData* data = [NSData dataWithContentsOfFile: [NSString stringWithFormat: @"%s/%s", argv[1], name.c_str()]];
            if (!data) continue;
            dispatch_data_t dd = dispatch_data_create(data.bytes, data.length, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError* error = nil;
            id<MTLLibrary> library = [device newLibraryWithData: dd error: &error];
            dispatch_release(dd);
            if (library) { ++ok; [library release]; continue; }
            ++failed;
            std::string message = error ? [[error localizedDescription] UTF8String] : "nil without error";
            auto& r = reasons[message];
            if (r.first++ == 0) r.second = name;
        }
        printf("libraries: %zu ok, %zu failed\n", ok, failed);
        std::vector<std::pair<int, std::string>> sorted;
        for (auto& r : reasons) sorted.push_back({r.second.first, r.first + "   [e.g. " + r.second.second + "]"});
        std::sort(sorted.rbegin(), sorted.rend());
        for (auto& s : sorted) printf("%6d  %s\n", s.first, s.second.c_str());
    }
    return 0;
}
