/* Nullable CFErrorRef output arguments must be safe and receive errors. */
#include <CoreServices/CoreServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <stdio.h>
int main(void) {
    CFArrayRef apps=LSCopyApplicationURLsForBundleIdentifier(NULL,NULL);
    CFErrorRef error=NULL;
    CFArrayRef withError=LSCopyApplicationURLsForBundleIdentifier(NULL,&error);
    int populated=error!=NULL;
    if(error)CFRelease(error);
    CFURLRef url=CFURLCreateWithString(NULL,CFSTR("darling-no-handler://test"),NULL);
    CFURLRef handler=LSCopyDefaultApplicationURLForURL(url,kLSRolesAll,NULL);
    CFURLRef content=LSCopyDefaultApplicationURLForContentType(CFSTR("org.darling.test.no-handler"),kLSRolesAll,NULL);
    printf("LAUNCHSERVICES_NULL_ERROR nullable_outputs=1 error_populated=%d missing_handlers=%d\n",populated,!handler&&!content);
    CFRelease(url);if(apps)CFRelease(apps);if(withError)CFRelease(withError);if(handler)CFRelease(handler);if(content)CFRelease(content);
    return !apps&&!withError&&populated&&!handler&&!content?0:1;
}
