"""Shared validation and Source-resource encoding for the localization catalogs."""
import re
import textwrap
import arabic_reshaper
from bidi.algorithm import get_display

LANGUAGES = set('arabic bulgarian schinese tchinese czech danish dutch english finnish french german greek hungarian indonesian italian japanese koreana malay norwegian polish portuguese brazilian romanian russian spanish latam swedish thai turkish ukrainian vietnamese'.split())
PRINTF = re.compile(r'%(?:[-+0 #]*\d*(?:\.\d+)?[sdiuoxXfgec]|%)')
PLACEHOLDERS = re.compile(r'\{\d+\}|%s[1-9]|%[A-Za-z_][A-Za-z_0-9]*%')

def template(text):
    index = 0
    def replace(match):
        nonlocal index
        if match[0] == '%%':
            return '%'
        result = '{%d}' % index
        index += 1
        return result
    return PRINTF.sub(replace, text)

def validate(key, source, translated):
    assert translated and '\0' not in translated, key
    assert sorted(PLACEHOLDERS.findall(source)) == sorted(PLACEHOLDERS.findall(translated)), (key, 'Changed placeholders')
    colors = lambda s: re.findall(r'[\x01-\x08]', s)
    assert colors(source) == colors(translated), (key, 'Changed chat colors')
    if source and ord(source[0]) in range(1, 9):
        assert translated[0] == source[0], (key, 'Missing initial chat color')

def display_text(language, text, wrap=False):
    if language != 'arabic':
        return text
    # Chat colors are positional protocol bytes and must stay beside their
    # original arguments. Shape literal spans without moving those controls.
    if re.search(r'[\x01-\x08]', text):
        pieces = re.split(r'(\{\d+\}|%s[1-9]|[\x01-\x08])', text)
        return ''.join(p if PLACEHOLDERS.fullmatch(p) or re.fullmatch(r'[\x01-\x08]', p) else get_display(arabic_reshaper.reshape(p), base_dir='R') for p in pieces)
    if PLACEHOLDERS.search(text):
        # Treat replacement markers as LTR atoms during bidi ordering. Their
        # indices can move visually while still referring to the same value.
        protected = PLACEHOLDERS.sub(lambda m: '\u202a' + m[0] + '\u202c', text)
        return get_display(arabic_reshaper.reshape(protected), base_dir='R')
    lines = textwrap.wrap(text, width=52, break_long_words=False, break_on_hyphens=False) if wrap else text.split('\n')
    return '\n'.join(get_display(arabic_reshaper.reshape(line), base_dir='R') for line in lines)

def escape(text):
    return text.replace('\\', '\\\\').replace('"', '\\"').replace('\r', '').replace('\n', r'\n').replace('\t', r'\t')

def resource(language, entries, wrap=False):
    lines = ['"lang"', '{', f'    "Language" "{language}"', '    "Tokens"', '    {']
    for key, value in entries.items():
        lines.append(f'        "{key}" "{escape(display_text(language, value, wrap))}"')
    lines += ['    }', '}', '']
    return '\r\n'.join(lines).encode('utf-16')
