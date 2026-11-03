"""Check catalog coverage, argument safety and distributable resources offline."""
import importlib.util
import json
import re
import unittest
from pathlib import Path
from localization import LANGUAGES, PLACEHOLDERS, display_text, template, validate

ROOT = Path(__file__).resolve().parents[1]
UI = json.loads((ROOT/'assets/localization/ui.json').read_text(encoding='utf8'))
MESSAGES = json.loads((ROOT/'assets/localization/messages.json').read_text(encoding='utf8'))

class LocalizationTests(unittest.TestCase):
    def test_each_client_has_every_key(self):
        self.assertEqual(set(UI['languages']), LANGUAGES - {'english'})
        for language, entries in UI['languages'].items():
            self.assertEqual(set(entries), set(UI['entries']), language)
            for key, value in entries.items():
                validate(language+'/'+key, UI['entries'][key]['english'], value)

    def test_source_references_have_fallbacks(self):
        keys = {key.lower() for key in UI['entries']}
        keys.update(('HL2MP_'+key).lower() for key in MESSAGES['keys'])
        files = list((ROOT/'src/game/client/hl2mp').rglob('*.cpp')) + list((ROOT/'src/game/client/hl2mp').rglob('*.h'))
        files += list((ROOT/'src/game/server/hl2mp').glob('*.cpp'))
        files += list((ROOT/'src/game/shared/hl2mp').glob('*.h')) + list((ROOT/'src/game/shared/hl2mp').glob('*.cpp'))
        files += list((ROOT/'game/mod_hl2mp/resource').glob('*.res'))
        for path in files:
            for key in re.findall(r'#(HL2MP_[A-Za-z0-9_]+)', path.read_text(encoding='utf8',errors='replace')):
                if key == 'HL2MP_Key_':
                    continue  # Engine key codes are resolved through a checked lookup.
                self.assertIn(key.lower(), keys, str(path)+': '+key)

    def test_formatter_reorders_without_executing_translations(self):
        self.assertEqual(template('Rating: %d (%+d).'), 'Rating: {0} ({1}).')
        validate('reordered', '{0} invited {1}', '{1} invited by {0}')
        for broken in ('{0}', '{0} {0}', '{0} {1} {2}'):
            with self.assertRaises(AssertionError):
                validate('broken', '{0} {1}', broken)
        for item in UI['entries'].values():
            if 'format' in item:
                self.assertEqual(item['english'], template(item['format']))
                self.assertNotIn('%n', item['format'])

    def test_chat_controls_and_arabic_arguments_survive(self):
        with self.assertRaises(AssertionError):
            validate('chat', '\x01%s1\x03%s2', '\x03%s1\x01%s2')
        text = '\x01اللاعب %s1\x03: %s2'
        displayed = display_text('arabic', text)
        self.assertEqual(PLACEHOLDERS.findall(text), PLACEHOLDERS.findall(displayed))
        self.assertEqual(displayed[0], '\x01')
        formatted = '{0} / {1}: ' + text[1:]
        self.assertCountEqual(PLACEHOLDERS.findall(formatted), PLACEHOLDERS.findall(display_text('arabic', formatted)))

    def test_generated_resources_are_current(self):
        spec = importlib.util.spec_from_file_location('builder', ROOT/'tools/build-localization.py')
        builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
        for path, expected in builder.resources(ROOT):
            self.assertEqual(path.read_bytes(), expected, path.name)
            if path.suffix == '.txt':
                self.assertTrue(expected.startswith(b'\xff\xfe'))

    def test_disconnect_sentences_have_punctuation(self):
        for language, values in MESSAGES['languages'].items():
            for key, value in zip(MESSAGES['keys'], values):
                if key not in ('Disconnected','QuitTitle','QuitBody','Quit','Cancel','OK','SignIn','Play'):
                    self.assertTrue(value.endswith(('.', '。','!', '?','！','？','؟')), (language,key))

if __name__ == '__main__':
    unittest.main()
