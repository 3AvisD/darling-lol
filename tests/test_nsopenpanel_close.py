"""Check that the patched Cocotron file panels always end their modal session and never throw on Open."""
import unittest
import test_runtime_compatibility as sources


def body(source, signature):
    return source.split(signature + ' {', 1)[1].split('\n}', 1)[0]


class FilePanelCloseChecks(unittest.TestCase):
    patched = sources.CompatibilityChecks.patched

    def save_panel(self):
        return self.patched('cocotron', 'AppKit/NSSavePanel.subproj/NSSavePanel.m')

    def open_panel(self):
        return self.patched('cocotron', 'AppKit/NSSavePanel.subproj/NSOpenPanel.m')

    def test_both_buttons_dismiss_through_one_helper(self):
        source = self.save_panel()
        dismiss = body(source, '- (void) _dismissWithCode: (NSModalResponse) code')
        self.assertIn('[NSApp modalWindow] == self', dismiss)
        self.assertIn('stopModalWithCode: code', dismiss)
        self.assertIn('orderOut', dismiss)
        self.assertIn('_dismissWithCode: NSCancelButton', body(source, '- (IBAction) _cancel: (id) sender'))
        self.assertIn('_dismissWithCode: NSOKButton', body(source, '- (IBAction) _selectFile: (id) sender'))

    def test_open_without_a_selection_cannot_throw(self):
        # Nothing selected used to hand nil to -_setFilename:, and NSOpenPanel boxed it in @[ ... ].
        self.assertNotIn('@[ filename ]];', self.open_panel().replace('[filename length] > 0 ? @[ filename ] : @[]', ''))
        select = body(self.save_panel(), '- (IBAction) _selectFile: (id) sender')
        self.assertNotIn('itemAtRow: [_outlineView selectedRow]', select)
        self.assertIn('@catch', select)
        self.assertIn('NSCancelButton', select.split('@catch', 1)[1])

    def test_open_panel_honours_what_it_may_choose(self):
        accept = body(self.open_panel(), '- (NSArray *) _acceptablePaths')
        self.assertIn('_canChooseDirectories', accept)
        self.assertIn('_canChooseFiles', accept)

    def test_escape_cancels(self):
        self.assertIn('[self _cancel: sender]', body(self.save_panel(), '- (void) cancelOperation: (id) sender'))


if __name__ == '__main__':
    unittest.main()
