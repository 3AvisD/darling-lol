"""Check that the patched X11 backend cannot re-map a window AppKit has hidden."""
import unittest
import test_runtime_compatibility as sources


def body(source, signature):
    return source.split(signature + ' {', 1)[1].split('\n}', 1)[0]


class HiddenWindowChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched

    def test_restack_and_make_key_map_only_visible_windows(self):
        source = self.patched('cocotron', 'AppKit/X11.backend/X11Window.m')
        self.assertIn('[_delegate isVisible]', body(source, '- (void) ensureMappedIfVisible'))
        for signature in ('- (void) placeAboveWindow: (NSInteger) otherNumber',
                          '- (void) placeBelowWindow: (NSInteger) otherNumber', '- (void) makeKey'):
            method = body(source, signature)
            self.assertIn('[self ensureMappedIfVisible]', method, signature)
            self.assertNotIn('[self ensureMapped]', method, signature)

    def test_ordering_marks_window_visible_before_placing_it(self):
        source = self.patched('cocotron', 'AppKit/NSWindow.m')
        ordering = source.split('relativeTo: (NSInteger) relativeTo\n{', 1)[1].split('\n}', 1)[0]
        for call in ('placeAboveWindow', 'placeBelowWindow'):
            self.assertLess(ordering.index('_isVisible = YES'), ordering.index(call))

    def test_hiding_reaches_the_platform_window_when_already_not_visible(self):
        source = self.patched('cocotron', 'AppKit/NSWindow.m')
        method = body(source, '- (void) setIsVisible: (BOOL) visible')
        self.assertIn('_isVisible == visible && visible', method)
        self.assertIn('hideWindow', method)


if __name__ == '__main__':
    unittest.main()
