#import <Foundation/Foundation.h>
#import <Metal/MTLVertexDescriptorInternal.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned setters;
@interface ObservedAttribute : MTLVertexAttributeDescriptor
@end
@implementation ObservedAttribute
- (void)setFormat:(MTLVertexFormat)value { ++setters; [super setFormat:value]; }
- (void)setOffset:(NSUInteger)value { ++setters; [super setOffset:value]; }
- (void)setBufferIndex:(NSUInteger)value { ++setters; [super setBufferIndex:value]; }
@end
static void verifyVertex(bool ok) { if (!ok) abort(); }
int main() {
 @autoreleasepool {
  MTLVertexDescriptor* vertex = [MTLVertexDescriptor new];
  // Populate sparse endpoints and hold a reference through reset.
  MTLVertexAttributeDescriptor* held = [vertex.attributes[30] retain];
  held.format = MTLVertexFormatFloat4; held.offset = 16; held.bufferIndex = 30;
  vertex.layouts[30].stride = 48; vertex.layouts[30].stepRate = 3;
  MTLVertexDescriptor* copy = [vertex copy];
  [vertex reset];
  verifyVertex(held.format == MTLVertexFormatInvalid && held.offset == 0 && held.bufferIndex == 0);
  verifyVertex(vertex.layouts[30].stride == 0 && vertex.layouts[30].stepRate == 1);
  auto converted = [copy asIndiumDescriptor];
  verifyVertex(converted.attributes.at(30).offset == 16 && converted.layouts.at(30).stride == 48);
  verifyVertex(converted.layouts.at(30).stepRate == 3);
  ObservedAttribute* custom = [ObservedAttribute new];
  custom.format = MTLVertexFormatFloat2; custom.bufferIndex = 4;
  vertex.attributes[4] = custom;
  verifyVertex([vertex asIndiumDescriptor].attributes.at(4).bufferIndex == 4);
  setters = 0; [vertex reset]; verifyVertex(setters == 3);
  vertex.attributes[4] = nil; [custom release]; [copy release]; [held release];
  // Repeated resets after lazy descriptors have been allocated, as in the game.
  for (unsigned i=0;i<12;++i) { vertex.attributes[i].format=MTLVertexFormatFloat3; vertex.layouts[i].stride=32; }
  mach_timebase_info_data_t tb; mach_timebase_info(&tb);
  const unsigned iterations=500000;
  uint64_t start=mach_absolute_time();
  for (unsigned i=0;i<iterations;++i) [vertex reset];
  uint64_t end=mach_absolute_time();
  printf("VERTEX_CHECK reset_ns=%.3f iterations=%u\n",(end-start)*(double)tb.numer/tb.denom/iterations,iterations);
  for (unsigned i=0;i<12;++i) { vertex.attributes[i].format=MTLVertexFormatFloat3; vertex.attributes[i].bufferIndex=i; vertex.layouts[i].stride=32; }
  start=mach_absolute_time(); size_t total=0;
  for (unsigned i=0;i<20000;++i) { auto d=[vertex asIndiumDescriptor]; total+=d.attributes.size(); }
  end=mach_absolute_time(); verifyVertex(total==240000);
  printf("VERTEX_CHECK convert_ns=%.3f PASS sparse/copy/held-reference/subclass\n",(end-start)*(double)tb.numer/tb.denom/20000);
  [vertex release];
 }
}
