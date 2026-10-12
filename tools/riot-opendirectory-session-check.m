/* Local directory lookups applications make at startup: default session,
 * node list, local/authentication node, user record and a user query. */
#import <Foundation/Foundation.h>
#import <OpenDirectory/OpenDirectory.h>
#include <stdio.h>

int main(void)
{
	@autoreleasepool {
		int session = 0, names = 0, node = 0, record = 0, guid = 0, query = 0, missing = 0, raised = 0;
		@try {
			NSError* error = nil;
			ODSession* defaultSession = [ODSession defaultSession];
			session = defaultSession != nil && defaultSession == [ODSession defaultSession];
			NSArray* nodeNames = [defaultSession nodeNamesAndReturnError:&error];
			names = [nodeNames count] == 1 && [[nodeNames firstObject] hasPrefix:@"/Local"];
			ODNode* authentication = [ODNode nodeWithSession:defaultSession type:kODNodeTypeAuthentication error:&error];
			ODNode* local = [ODNode nodeWithSession:defaultSession type:kODNodeTypeLocalNodes error:&error];
			node = authentication != nil && [[local nodeName] isEqualToString:@"/Local/Default"]
			    && [ODNode nodeWithSession:defaultSession name:@"/LDAPv3/missing" error:&error] == nil && error != nil;
			ODRecord* user = [authentication recordWithRecordType:kODRecordTypeUsers name:NSUserName()
			                                           attributes:kODAttributeTypeRecordName error:&error];
			record = [[user recordName] isEqualToString:NSUserName()]
			      && [[user valuesForAttribute:@"dsAttrTypeStandard:OriginalAuthenticationAuthority" error:&error] count] == 0;
			ODQuery* search = [ODQuery queryWithNode:local forRecordTypes:kODRecordTypeUsers
			                               attribute:kODAttributeTypeRecordName matchType:kODMatchEqualTo
			                             queryValues:NSUserName() returnAttributes:kODAttributeTypeGUID
			                          maximumResults:1 error:&error];
			NSArray* results = [search resultsAllowingPartial:NO error:&error];
			query = [results count] == 1;
			NSString* value = [[[results firstObject] valuesForAttribute:kODAttributeTypeGUID error:&error] firstObject];
			guid = [[NSUUID alloc] initWithUUIDString:value] != nil
			    && [value isEqualToString:[[[user valuesForAttribute:kODAttributeTypeGUID error:&error] firstObject] description]];
			ODQuery* absent = [ODQuery queryWithNode:local forRecordTypes:kODRecordTypeUsers
			                               attribute:kODAttributeTypeRecordName matchType:kODMatchEqualTo
			                             queryValues:@"riot-check-missing-user" returnAttributes:nil
			                          maximumResults:0 error:&error];
			NSArray* everyone = [[ODQuery queryWithNode:local forRecordTypes:@[kODRecordTypeUsers]
			                                  attribute:nil matchType:kODMatchAny queryValues:nil
			                           returnAttributes:nil maximumResults:0 error:&error]
			                     resultsAllowingPartial:NO error:&error];
			missing = [[absent resultsAllowingPartial:NO error:&error] count] == 0 && [everyone count] >= 1
			       && [ODQuery queryWithNode:nil forRecordTypes:kODRecordTypeUsers attribute:nil matchType:kODMatchAny
			                     queryValues:nil returnAttributes:nil maximumResults:0 error:&error] == nil;
		} @catch (NSException* exception) {
			raised = 1;
			printf("OPENDIRECTORY_EXCEPTION name=%s reason=%s\n", [[exception name] UTF8String], [[exception reason] UTF8String]);
		}
		printf("OPENDIRECTORY session=%d names=%d node=%d record=%d query=%d guid=%d missing=%d raised=%d\n",
		       session, names, node, record, query, guid, missing, raised);
		fflush(stdout);
		return !(session && names && node && record && query && guid && missing && !raised);
	}
}
