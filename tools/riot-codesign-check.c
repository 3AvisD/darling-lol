/* Read-only checks against Darling's real Security framework. */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/SecStaticCode.h>
#include <Security/SecCode.h>
#include <Security/CMSDecoder.h>
#include <Security/SecRequirement.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>

int main(int argc, char **argv) {
    Dl_info image;
    if (dladdr((const void *)SecStaticCodeCheckValidityWithErrors, &image)) {
        printf("CODESIGN_IMAGE base=%p validation=%p\n", image.dli_fbase,
            (const void *)SecStaticCodeCheckValidityWithErrors);
        printf("CODESIGN_IMAGE path=%s framework_path=%s\n", image.dli_fname,
            getenv("DYLD_FRAMEWORK_PATH") ? getenv("DYLD_FRAMEWORK_PATH") : "unset");
        fflush(stdout);
    }
    if (getenv("CODESIGN_WAIT")) sleep(8);
    int failed = 0;
    const char *requirements[] = {"anchor apple generic", "never", "identifier \"com.riotgames.LeagueClient\"", "identifier ("};
    for (int i = 0; i < 4; ++i) {
        CFStringRef text = CFStringCreateWithCString(NULL, requirements[i], kCFStringEncodingUTF8);
        SecRequirementRef requirement = NULL;
        OSStatus status = SecRequirementCreateWithString(text, 0, &requirement);
        printf("CODESIGN parser_case=%d status=%d expected=%s\n", i, (int)status,
            i == 3 ? "reject" : "accept");
        failed |= i == 3 ? status == 0 : status != 0;
        if (requirement) CFRelease(requirement);
        CFRelease(text);
    }
    fflush(stdout);
    for (int i = 1; i < argc; ++i) {
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL,
            (const UInt8 *)argv[i], strlen(argv[i]), false);
        SecStaticCodeRef code = NULL;
        OSStatus created = SecStaticCodeCreateWithPath(url, 0, &code);
        printf("CODESIGN target=%d create=%d\n", i, (int)created);
        fflush(stdout);
        if (!created && code) {
            CFDictionaryRef info = NULL;
            OSStatus copied = SecCodeCopySigningInformation(code,
                kSecCSSigningInformation, &info);
            CFArrayRef certs = info ? CFDictionaryGetValue(info,
                kSecCodeInfoCertificates) : NULL;
            printf("CODESIGN target=%d information=%d certificates=%ld CMS=%d\n",
                i, (int)copied, certs ? CFArrayGetCount(certs) : 0L,
                info && CFDictionaryGetValue(info, kSecCodeInfoCMS) != NULL);
            fflush(stdout);
            CFDataRef cms = info ? CFDictionaryGetValue(info, kSecCodeInfoCMS) : NULL;
            if (cms) {
                CMSDecoderRef decoder = NULL;
                OSStatus made = CMSDecoderCreate(&decoder);
                OSStatus updated = made ? made : CMSDecoderUpdateMessage(decoder,
                    CFDataGetBytePtr(cms), CFDataGetLength(cms));
                OSStatus finished = updated ? updated : CMSDecoderFinalizeMessage(decoder);
                CFArrayRef decoded = NULL;
                OSStatus extracted = finished ? finished : CMSDecoderCopyAllCerts(decoder, &decoded);
                printf("CODESIGN target=%d CMS_create=%d update=%d finalize=%d certs=%d count=%ld\n",
                    i, (int)made, (int)updated, (int)finished, (int)extracted,
                    decoded ? CFArrayGetCount(decoded) : 0L);
                if (decoded) CFRelease(decoded);
                if (decoder) CFRelease(decoder);
                fflush(stdout);
            }
            CFErrorRef error = NULL;
            OSStatus valid = SecStaticCodeCheckValidityWithErrors(code, 0, NULL, &error);
            printf("CODESIGN target=%d validity=%d error_code=%ld\n", i,
                (int)valid, error ? CFErrorGetCode(error) : 0L);
            fflush(stdout);
            if (valid == 0) {
                SecRequirementRef never = NULL;
                OSStatus parsed = SecRequirementCreateWithString(CFSTR("never"), 0, &never);
                OSStatus rejected = parsed ? parsed : SecStaticCodeCheckValidity(code, 0, never);
                printf("CODESIGN target=%d false_requirement=%d expected=reject\n", i, (int)rejected);
                failed |= parsed != 0 || rejected == 0;
                if (never) CFRelease(never);
            }
            if (error) {
                CFStringRef description = CFErrorCopyDescription(error);
                char text[1024] = {0};
                if (description && CFStringGetCString(description, text,
                        sizeof(text), kCFStringEncodingUTF8))
                    printf("CODESIGN target=%d description=%s\n", i, text);
                if (description) CFRelease(description);
            }
            failed |= copied != 0 || valid != 0;
            if (error) CFRelease(error);
            if (info) CFRelease(info);
            CFRelease(code);
        } else failed = 1;
        if (url) CFRelease(url);
    }
    return failed;
}
