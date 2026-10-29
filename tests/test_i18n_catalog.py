import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CatalogTest(unittest.TestCase):
    def test_all_app_messages_have_translations_and_matching_placeholders(self):
        catalog = json.loads((ROOT / 'resources/i18n.json').read_text(encoding='utf-8'))
        count = 0
        for path in (ROOT / 'src').glob('*.cpp'):
            for match in re.finditer(r'I18n::(?:text|utf8)\(("(?:\\.|[^"\\])*")\)', path.read_text(encoding='utf-8')):
                source = json.loads(match.group(1))
                count += 1
                self.assertIn(source, catalog, (path.name, source))
                for language in ('en', 'ja', 'zh'):
                    translated = catalog[source].get(language)
                    self.assertTrue(translated, (source, language))
                    self.assertEqual(sorted(re.findall(r'%\d+', source)), sorted(re.findall(r'%\d+', translated)), (source, language))
                    self.assertFalse(re.search('[가-힣]', translated), (source, language))
        self.assertGreater(count, 240)

    def test_no_untranslated_korean_literals(self):
        for path in (ROOT / 'src').glob('*.cpp'):
            source = path.read_text(encoding='utf-8')
            for match in re.finditer(r'"(?:\\.|[^"\\])*"', source):
                if not re.search('[가-힣]', match.group()):
                    continue
                if match.group() == '"한국어"':  # Native language name in the picker.
                    continue
                self.assertRegex(source[max(0, match.start() - 15):match.start()], r'I18n::(?:text|utf8)\($', (path.name, match.group()))


if __name__ == '__main__':
    unittest.main()
