"""Build Source localization resources from the UTF-8 translation catalog.

Language identifiers follow Steam's full-platform API language list:
https://partner.steamgames.com/doc/store/localization/languages
Translations should receive native-speaker review before release.
Install tools/asset-requirements.txt before running this builder or --check.
"""
import argparse
import json
import textwrap
from pathlib import Path
import arabic_reshaper
from bidi.algorithm import get_display

LANGUAGES = set('arabic bulgarian schinese tchinese czech danish dutch english finnish french german greek hungarian indonesian italian japanese koreana malay norwegian polish portuguese brazilian romanian russian spanish latam swedish thai turkish ukrainian vietnamese'.split())

def display_text(language, text):
    if language != 'arabic':
        return text
    # VGUI paints individual glyphs left-to-right and has no shaping/bidi pass.
    # Keep logical Unicode in the catalog. Shape/reorder each short logical line
    # separately so paragraph wrapping cannot reverse the order of sentences.
    # 52 characters fit the dialog's minimum 544px text column at 18px.
    lines = textwrap.wrap(text, width=52, break_long_words=False, break_on_hyphens=False)
    return r'\n'.join(get_display(arabic_reshaper.reshape(line), base_dir='R') for line in lines)

def resources(root):
    catalog = json.loads((root / 'assets/localization/messages.json').read_text(encoding='utf-8'))
    assert set(catalog['languages']) == LANGUAGES, 'Missing or unknown Steam language'
    assert len(catalog['keys']) == len(set(catalog['keys']))
    for language, values in catalog['languages'].items():
        assert len(values) == len(catalog['keys']), language
        lines = ['"lang"', '{', f'    "Language" "{language}"', '    "Tokens"', '    {']
        for key, value in zip(catalog['keys'], values):
            assert value and '\n' not in value and '"' not in value and '\\' not in value, (language, key)
            if key not in ('QuitBody',):
                assert not value.endswith(('.', '。', '！')), (language, key, 'Engine adds disconnect punctuation')
            lines.append(f'        "HL2MP_{key}" "{display_text(language, value)}"')
        lines += ['    }', '}', '']
        yield root / f'game/mod_hl2mp/resource/hl2mp_messages_{language}.txt', '\r\n'.join(lines).encode('utf-16')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    count = 0
    for path, data in resources(Path(__file__).resolve().parents[1]):
        if args.check:
            assert path.read_bytes() == data, f'Regenerate {path.name}'
        else:
            path.write_bytes(data)
        count += 1
    print(f'{count} localization files checked' if args.check else f'{count} localization files built')

if __name__ == '__main__':
    main()
