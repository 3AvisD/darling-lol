/* How NSURLSessionConfiguration behaves on this platform, probed only through
 * runtime lookups so a broken class cannot abort the probe before it reports. */
#import <Foundation/Foundation.h>
#include <objc/runtime.h>
#include <stdio.h>

int main(void)
{
	@autoreleasepool {
		Class byName = objc_getClass("NSURLSessionConfiguration");
		Class lookup = NSClassFromString(@"NSURLSessionConfiguration");
		printf("URLSESSION getClass=%d classFromString=%d\n", byName != Nil, lookup != Nil);
		fflush(stdout);
		Method method = byName ? class_getClassMethod(byName, @selector(ephemeralSessionConfiguration)) : NULL;
		Method defaults = byName ? class_getClassMethod(byName, @selector(defaultSessionConfiguration)) : NULL;
		printf("URLSESSION ephemeral_method=%d default_method=%d name=%s\n", method != NULL, defaults != NULL,
		       byName ? class_getName(byName) : "-");
		fflush(stdout);
		Class session = objc_getClass("NSURLSession");
		printf("URLSESSION session_class=%d\n", session != Nil);
		fflush(stdout);
	}
	return 0;
}
