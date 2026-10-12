/* SecTrust evaluation of an untrusted self-signed server certificate must
 * complete and report a trust failure, not fail as an API error. */
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#include <Security/SecPolicySearch.h>
#include <Security/cssmapple.h>
#include <Security/oidsalg.h>
#include <Security/oidscert.h>
#include <Security/cssmapi.h>
#include <Security/SecKeychain.h>
#include <Security/SecTrustSettings.h>
#include <stdio.h>
#include <string.h>

static int run(int argc, char** argv)
{
	unsigned char bytes[8192];
	FILE* file = argc >= 2 ? fopen(argv[1], "rb") : NULL;
	if (!file) return 3;
	size_t size = fread(bytes, 1, sizeof(bytes), file);
	fclose(file);
	CFDataRef data = CFDataCreate(NULL, bytes, size);
	SecCertificateRef certificate = SecCertificateCreateWithData(NULL, data);
	printf("SECTRUST certificate=%d\n", certificate != NULL);
	fflush(stdout);
	if (!certificate) return 1;
	const void* values[] = { certificate };
	CFArrayRef certificates = CFArrayCreate(NULL, values, 1, &kCFTypeArrayCallBacks);
	SecPolicyRef policy = SecPolicyCreateSSL(true, CFSTR("127.0.0.1"));
	SecTrustRef trust = NULL;
	OSStatus created = SecTrustCreateWithCertificates(certificates, policy, &trust);
	printf("SECTRUST policy=%d create=%d trust=%d\n", policy != NULL, (int)created, trust != NULL);
	fflush(stdout);
	if (created != 0 || !trust) return 1;
	SecTrustResultType result = kSecTrustResultInvalid;
	OSStatus evaluated = SecTrustEvaluate(trust, &result);
	printf("SECTRUST evaluate=%d result=%d\n", (int)evaluated, (int)result);
	fflush(stdout);
	/* The legacy result accessors a Chromium 108 era verifier reads after evaluating. */
	CFArrayRef chain = NULL;
	CSSM_TP_APPLE_EVIDENCE_INFO* chainInfo = NULL;
	SecTrustResultType legacyResult = kSecTrustResultInvalid;
	OSStatus legacy = SecTrustGetResult(trust, &legacyResult, &chain, &chainInfo);
	printf("SECTRUST get_result=%d chain=%ld info=%d status_bits=%#x\n", (int)legacy,
	       chain ? (long)CFArrayGetCount(chain) : -1L, chainInfo != NULL, chainInfo ? (unsigned)chainInfo[0].StatusBits : 0);
	fflush(stdout);
	if (chain && CFArrayGetCount(chain) > 0) {
		/* A verifier inspects the returned chain through the legacy CL handle. */
		SecCertificateRef chainCertificate = (SecCertificateRef)CFArrayGetValueAtIndex(chain, 0);
		CSSM_CL_HANDLE chainHandle = 0;
		OSStatus chainStatus = SecCertificateGetCLHandle(chainCertificate, &chainHandle);
		printf("SECTRUST chain_cl_handle=%d same_object=%d\n", (int)chainStatus, chainCertificate == certificate);
		fflush(stdout);
	}
	OSStatus cssmResult = 0;
	OSStatus cssm = SecTrustGetCssmResultCode(trust, &cssmResult);
	printf("SECTRUST cssm_status=%d cssm_result=%d\n", (int)cssm, (int)cssmResult);
	fflush(stdout);
	SecPolicyRef revocation = SecPolicyCreateRevocation(kSecRevocationUseAnyAvailableMethod);
	SecPolicyRef basic = SecPolicyCreateBasicX509();
	printf("SECTRUST revocation_policy=%d basic_policy=%d\n", revocation != NULL, basic != NULL);
	fflush(stdout);
	CFArrayRef keychains = CFArrayCreate(NULL, NULL, 0, &kCFTypeArrayCallBacks);
	SecTrustRef second = NULL;
	const void* policies[] = { basic, policy };
	CFArrayRef policyArray = CFArrayCreate(NULL, policies, 2, &kCFTypeArrayCallBacks);
	OSStatus secondCreate = SecTrustCreateWithCertificates(certificates, policyArray, &second);
	OSStatus setKeychains = second ? SecTrustSetKeychains(second, keychains) : -1;
	SecTrustResultType secondResult = kSecTrustResultInvalid;
	OSStatus secondEvaluate = second ? SecTrustEvaluate(second, &secondResult) : -1;
	printf("SECTRUST second_create=%d set_keychains=%d evaluate=%d result=%d\n", (int)secondCreate,
	       (int)setKeychains, (int)secondEvaluate, (int)secondResult);
	fflush(stdout);
	/* Revocation policy built through the CSSM policy search, with CRL options. */
	SecPolicySearchRef search = NULL;
	OSStatus searchStatus = SecPolicySearchCreate(CSSM_CERT_X_509v3, &CSSMOID_APPLE_TP_REVOCATION_CRL, NULL, &search);
	SecPolicyRef crlPolicy = NULL;
	OSStatus nextStatus = search ? SecPolicySearchCopyNext(search, &crlPolicy) : -1;
	/* CSSM_APPLE_TP_CRL_OPTIONS, version 0 */
	struct { uint32_t Version; uint32_t CrlFlags; void* crlStore; } crlOptions;
	memset(&crlOptions, 0, sizeof(crlOptions));
	CSSM_DATA optionsData = { sizeof(crlOptions), (uint8_t*)&crlOptions };
	OSStatus setValue = crlPolicy ? SecPolicySetValue(crlPolicy, &optionsData) : -1;
	printf("SECTRUST policy_search=%d copy_next=%d set_value=%d\n", (int)searchStatus, (int)nextStatus, (int)setValue);
	fflush(stdout);
	if (crlPolicy) {
		const void* legacyPolicies[] = { basic, policy, crlPolicy };
		CFArrayRef legacyArray = CFArrayCreate(NULL, legacyPolicies, 3, &kCFTypeArrayCallBacks);
		SecTrustRef third = NULL;
		OSStatus thirdCreate = SecTrustCreateWithCertificates(certificates, legacyArray, &third);
		SecTrustResultType thirdResult = kSecTrustResultInvalid;
		OSStatus thirdEvaluate = third ? SecTrustEvaluate(third, &thirdResult) : -1;
		OSStatus thirdCssm = 0;
		OSStatus thirdCode = third ? SecTrustGetCssmResultCode(third, &thirdCssm) : -1;
		printf("SECTRUST crl_trust_create=%d evaluate=%d result=%d cssm_status=%d cssm_result=%d\n", (int)thirdCreate,
		       (int)thirdEvaluate, (int)thirdResult, (int)thirdCode, (int)thirdCssm);
		fflush(stdout);
	}
	/* Remaining legacy calls a Chromium 108 verifier makes. */
	CFArrayRef searchList = NULL;
	OSStatus searchListStatus = SecKeychainCopySearchList(&searchList);
	printf("SECTRUST keychain_search_list=%d count=%ld\n", (int)searchListStatus, searchList ? (long)CFArrayGetCount(searchList) : -1L);
	fflush(stdout);
	SecTrustRef fourth = NULL;
	SecTrustCreateWithCertificates(certificates, policyArray, &fourth);
	OSStatus parameters = fourth ? SecTrustSetParameters(fourth, CSSM_TP_ACTION_DEFAULT, NULL) : -1;
	OSStatus anchorsOnly = fourth ? SecTrustSetAnchorCertificatesOnly(fourth, false) : -1;
	SecTrustResultType fourthResult = kSecTrustResultInvalid;
	OSStatus fourthEvaluate = fourth ? SecTrustEvaluate(fourth, &fourthResult) : -1;
	printf("SECTRUST set_parameters=%d anchors_only=%d evaluate=%d result=%d\n", (int)parameters, (int)anchorsOnly,
	       (int)fourthEvaluate, (int)fourthResult);
	fflush(stdout);
	CSSM_CL_HANDLE clHandle = 0;
	OSStatus clStatus = SecCertificateGetCLHandle(certificate, &clHandle);
	CSSM_DATA certData = { 0, NULL };
	OSStatus dataStatus = SecCertificateGetData(certificate, &certData);
	CSSM_HANDLE cached = 0;
	CSSM_RETURN cacheStatus = clStatus == 0 ? CSSM_CL_CertCache(clHandle, &certData, &cached) : -1;
	printf("SECTRUST cl_handle=%d get_data=%d cert_cache=%d\n", (int)clStatus, (int)dataStatus, (int)cacheStatus);
	fflush(stdout);
	if (cacheStatus == 0) {
		uint32 fields = 0;
		CSSM_HANDLE results = 0;
		CSSM_DATA_PTR field = NULL;
		CSSM_RETURN fieldStatus = CSSM_CL_CertGetFirstCachedFieldValue(clHandle, cached, &CSSMOID_X509V1SignatureAlgorithm,
		                                                               &results, &fields, &field);
		printf("SECTRUST cached_field=%d fields=%u\n", (int)fieldStatus, (unsigned)fields);
		fflush(stdout);
	}
	CFArrayRef domainCerts = NULL;
	OSStatus domainStatus = SecTrustSettingsCopyCertificates(kSecTrustSettingsDomainUser, &domainCerts);
	printf("SECTRUST settings_certificates=%d\n", (int)domainStatus);
	fflush(stdout);
	CFArrayRef settings = NULL;
	OSStatus settingsStatus = SecTrustSettingsCopyTrustSettings(certificate, kSecTrustSettingsDomainUser, &settings);
	printf("SECTRUST trust_settings=%d\n", (int)settingsStatus);
	fflush(stdout);
	CFArrayRef anchors = NULL;
	OSStatus anchorStatus = SecTrustCopyAnchorCertificates(&anchors);
	printf("SECTRUST anchors_status=%d anchors=%ld\n", (int)anchorStatus, anchors ? (long)CFArrayGetCount(anchors) : -1L);
	return !(evaluated == 0 && (result == kSecTrustResultRecoverableTrustFailure || result == kSecTrustResultDeny || result == kSecTrustResultFatalTrustFailure));
}

#include <pthread.h>
#include <unistd.h>
static int threadArgc;
static char** threadArgv;
static void* threadMain(void* unused)
{
	int result = run(threadArgc, threadArgv);
	fflush(stdout);
	_exit(result);
}

int main(int argc, char** argv)
{
	if (argc >= 3) {
		pthread_t thread;
		void* result = NULL;
		threadArgc = argc; threadArgv = argv;
		pthread_create(&thread, NULL, threadMain, NULL);
		/* the main thread services its run loop, as an application's does */
		CFRunLoopRun();
		return 2;
	}
	return run(argc, argv);
}
