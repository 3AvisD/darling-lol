/* Time a SecTrust evaluation of a real server chain (leaf first) for a host. */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv)
{
	if (argc < 3) return 3;
	CFStringRef host = CFStringCreateWithCString(NULL, argv[1], kCFStringEncodingUTF8);
	CFMutableArrayRef certificates = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	for (int i = 2; i < argc; i++) {
		static unsigned char bytes[16384];
		FILE* file = fopen(argv[i], "rb");
		if (!file) return 3;
		size_t size = fread(bytes, 1, sizeof(bytes), file);
		fclose(file);
		CFDataRef data = CFDataCreate(NULL, bytes, size);
		SecCertificateRef certificate = SecCertificateCreateWithData(NULL, data);
		if (certificate) CFArrayAppendValue(certificates, certificate);
	}
	SecPolicyRef policy = SecPolicyCreateSSL(true, host);
	SecTrustRef trust = NULL;
	CFTypeRef policies = policy;
	if (getenv("CHAIN_REVOCATION")) {
		SecPolicyRef revocation = SecPolicyCreateRevocation(kSecRevocationUseAnyAvailableMethod | kSecRevocationRequirePositiveResponse * (getenv("CHAIN_REVOCATION")[0] == '2'));
		const void* values[] = { policy, revocation };
		policies = CFArrayCreate(NULL, values, 2, &kCFTypeArrayCallBacks);
	}
	OSStatus created = SecTrustCreateWithCertificates(certificates, policies, &trust);
	CFAbsoluteTime start = CFAbsoluteTimeGetCurrent();
	SecTrustResultType result = kSecTrustResultInvalid;
	OSStatus evaluated = trust ? SecTrustEvaluate(trust, &result) : -1;
	double first = CFAbsoluteTimeGetCurrent() - start;
	start = CFAbsoluteTimeGetCurrent();
	CFArrayRef anchors = NULL;
	OSStatus anchorStatus = SecTrustCopyAnchorCertificates(&anchors);
	double anchorTime = CFAbsoluteTimeGetCurrent() - start;
	printf("SECCHAIN certificates=%ld create=%d evaluate=%d result=%d seconds=%.2f anchors_status=%d anchors=%ld anchor_seconds=%.2f\n",
	       (long)CFArrayGetCount(certificates), (int)created, (int)evaluated, (int)result, first,
	       (int)anchorStatus, anchors ? (long)CFArrayGetCount(anchors) : -1L, anchorTime);
	return !(evaluated == 0);
}
