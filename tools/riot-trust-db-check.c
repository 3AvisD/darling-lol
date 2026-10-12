#include <CoreFoundation/CoreFoundation.h>
#include <utilities/SecDb.h>
#include <utilities/SecFileLocations.h>
#include <Security/Security.h>
#include <trust/trustd/SecOCSPCache.h>
#include <trust/trustd/SecOCSPRequest.h>
#include <stdio.h>
#include <unistd.h>

extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);

int main(int argc, char **argv)
{
    void *pool = objc_autoreleasePoolPush();
    if (argc >= 2) {
        SecSetCustomHomeURLString(CFSTR("/tmp/riot-trust-cache-fixture"));
        FILE *fixture = fopen(argv[1], "rb");
        unsigned char bytes[4096];
        if (!fixture) return 3;
        size_t size = fread(bytes, 1, sizeof(bytes), fixture);
        fclose(fixture);
        CFDataRef data = CFDataCreate(NULL, bytes, size);
        SecCertificateRef certificate = SecCertificateCreateWithData(NULL, data);
        CFRelease(data);
        if (!certificate) return 4;
        if (argc == 3) {
            SecPolicyRef policy = SecPolicyCreateBasicX509();
            SecTrustRef trust = NULL;
            OSStatus status = SecTrustCreateWithCertificates(certificate, policy, &trust);
            SecTrustResultType result = kSecTrustResultInvalid;
            if (status == errSecSuccess) status = SecTrustEvaluate(trust, &result);
            printf("TRUST_EVALUATE status=%d result=%u\n", (int)status, (unsigned)result);
            if (trust) CFRelease(trust);
            CFRelease(policy);
            CFRelease(certificate);
            return status == errSecSuccess ? 0 : 7;
        }
        SecOCSPRequestRef request = SecOCSPRequestCreate(certificate, certificate);
        if (!request) return 5;
        for (unsigned i = 0; i < 10; i++) {
            SecOCSPResponseRef response = SecOCSPCacheCopyMatching(request, NULL);
            if (response) return 6; // The fixture's cache must be empty.
            printf("OCSP_CACHE empty_lookup=%u\n", i + 1);
            fflush(stdout);
        }
        SecOCSPCacheCloseDB();
        SecOCSPRequestFinalize(request);
        CFRelease(certificate);
        printf("OCSP_CACHE lookups=10 pass=1\n");
        return 0;
    }
    const char *path = "/tmp/riot-trust-db-check.sqlite3";
    unlink(path);
    _SecDbServerSetup();
    SecDbRef db = SecDbCreate(CFSTR("/tmp/riot-trust-db-check.sqlite3"),
                            0600, true, true, true, true, 1,
        ^bool(SecDbRef owner, SecDbConnectionRef connection, bool created,
              bool *again, CFErrorRef *error) {
            __block bool ok = true;
            if (created) {
                ok = SecDbTransaction(connection, kSecDbExclusiveTransactionType, error, ^(bool *commit) {
                    ok = SecDbExec(connection, CFSTR("CREATE TABLE fixture(value INTEGER)"), error);
                    *commit = ok;
                });
            }
            return ok;
        });
    if (!db) return 1;
    __block unsigned reads = 0;
    CFErrorRef error = NULL;
    __block int expected_sql_error = 0;
    SecDbPerformWrite(db, &error, ^(SecDbConnectionRef connection) {
        CFErrorRef sql_error = NULL;
        if (!SecDbWithSQL(connection, CFSTR("SELECT missing_column FROM missing_table"), &sql_error, NULL)
                && sql_error) {
            expected_sql_error = 1;
            for (unsigned i = 0; i < 1000; i++) {
                CFStringRef text = CFStringCreateWithFormat(NULL, NULL, CFSTR("fixture: %@"), sql_error);
                CFRelease(text);
            }
            CFRelease(sql_error);
        }
    });
    for (unsigned i = 0; i < 10; i++) {
        if (!SecDbPerformRead(db, &error, ^(SecDbConnectionRef connection) {
            if (connection) reads++;
        })) break;
    }
    CFRelease(db);
    if (error) CFRelease(error);
    unlink(path);
    objc_autoreleasePoolPop(pool);
    printf("TRUST_DB sql_error_released=%d reads=%u pass=%d\n", expected_sql_error, reads,
           expected_sql_error && reads == 10);
    return expected_sql_error && reads == 10 ? 0 : 2;
}
