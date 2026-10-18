#import <Foundation/Foundation.h>
#import <CoreText/CTFont.h>
#import <CoreText/CTFontDescriptor.h>
#import <CoreText/CTFontManager.h>
#include <stdio.h>
#include <stdlib.h>

static void verifyFontList(BOOL pass, const char *message) {
    if (!pass) { fprintf(stderr, "CORETEXT_FONT_LIST FAIL: %s\n", message); exit(1); }
}

int main(void) {
    @autoreleasepool {
        CFArrayRef families = CTFontManagerCopyAvailableFontFamilyNames();
        verifyFontList(families && CFArrayGetCount(families) > 0, "installed font families");
        const void *key = kCTFontFamilyNameAttribute;
        CFSetRef mandatory = CFSetCreate(NULL, &key, 1, &kCFTypeSetCallBacks);
        unsigned faces = 0;
        for (NSString *family in (NSArray *)families) {
            NSDictionary *attributes = [NSDictionary dictionaryWithObject:family forKey:(id)key];
            CTFontDescriptorRef query = CTFontDescriptorCreateWithAttributes((CFDictionaryRef)attributes);
            CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors(query, mandatory);
            verifyFontList(matches && CFArrayGetCount(matches) > 0, "family resolves to installed faces");
            for (CFIndex i = 0; i < CFArrayGetCount(matches); ++i) {
                CTFontDescriptorRef match = (CTFontDescriptorRef)CFArrayGetValueAtIndex(matches, i);
                CFStringRef name = CTFontDescriptorCopyAttribute(match, kCTFontFamilyNameAttribute);
                verifyFontList(name && CFEqual(name, (CFStringRef)family), "mandatory family matches exactly");
                CFStringRef localized = CTFontDescriptorCopyLocalizedAttribute(match, kCTFontFamilyNameAttribute, NULL);
                verifyFontList(localized && CFGetTypeID(localized) == CFStringGetTypeID(), "localized lookup with null output");
                CFRelease(localized); CFRelease(name); ++faces;
            }
            CTFontDescriptorRef first = CTFontDescriptorCreateMatchingFontDescriptor(query, mandatory);
            verifyFontList(first != NULL, "single matching descriptor");
            CFRelease(query); CFRelease(matches);
            /* The single-match result must outlive its temporary array. */
            CFTypeRef name = CTFontDescriptorCopyAttribute(first, kCTFontNameAttribute);
            verifyFontList(name != NULL, "retained single descriptor and normalized PostScript name");
            /* Chromium pre-fills the language output with a sentinel and reads it when non-NULL. */
            CTFontRef font = CTFontCreateWithFontDescriptor(first, 12, NULL);
            CFStringRef sentinel = CFSTR("sentinel"), language = sentinel;
            CFStringRef familyName = CTFontCopyLocalizedName(font, kCTFontFamilyNameKey, &language);
            verifyFontList(language != sentinel, "localized font name initializes language output");
            verifyFontList(familyName && CFEqual(familyName, (CFStringRef)family), "localized font name is the family");
            CFRelease(familyName); CFRelease(font);
            CFRelease(name); CFRelease(first);
        }
        NSDictionary *missing = [NSDictionary dictionaryWithObject:@"DarlingMissingFont-7259AEB1" forKey:(id)key];
        CTFontDescriptorRef query = CTFontDescriptorCreateWithAttributes((CFDictionaryRef)missing);
        CFArrayRef matches = CTFontDescriptorCreateMatchingFontDescriptors(query, mandatory);
        verifyFontList(!matches || !CFArrayGetCount(matches), "missing mandatory family has no substitutes");
        verifyFontList(!CTFontDescriptorCreateMatchingFontDescriptor(query, mandatory), "no single match for missing font");
        CFStringRef language = CFSTR("sentinel");
        verifyFontList(!CTFontDescriptorCopyLocalizedAttribute(query, kCTFontStyleNameAttribute, &language) && !language,
              "missing attribute initializes language output");
        if (matches) CFRelease(matches);
        CFRelease(query); CFRelease(mandatory);
        printf("CORETEXT_FONT_LIST pass=1 families=%ld faces=%u\n", CFArrayGetCount(families), faces);
        CFRelease(families);
    }
    return 0;
}
