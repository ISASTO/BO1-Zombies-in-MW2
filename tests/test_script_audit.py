import importlib.util
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('audit', Path(__file__).parents[1]/'tools/audit_bo1_scripts.py')
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class ScriptAuditTests(unittest.TestCase):
    def test_comments_strings_and_line_anchors(self):
        text = '''// maps\\bad::main(); %fake PlaySound("fake")
#include maps\\utility;
#using_animtree ("owned_test_tree");
main()
{
    value = "maps\\fake::main() { //";
    self PlaySound("owned_test_alias");
    self setanim(%owned_test_anim);
    maps\\helper::run();
    /* maps\\missing::main(); */
    /# maps\\debug::main(); #/
}
'''
        report = audit.inspect_script('maps/test.gsc', text.encode())
        self.assertEqual(report['includes'], ['maps/utility.gsc'])
        self.assertEqual(report['script_calls'], ['maps/helper.gsc'])
        self.assertEqual(report['animtrees'], ['owned_test_tree'])
        self.assertEqual(report['animation_symbols'], ['owned_test_anim'])
        self.assertEqual(report['literal_sound_aliases'], ['owned_test_alias'])
        self.assertEqual(report['functions'][0], {'name': 'main', 'start_line': 4, 'end_line': 12})
        self.assertNotIn('source', report)

    def test_override_precedence_and_missing_closure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for zone in ('base.ff', 'patch.ff'):
                (root/zone/'maps').mkdir(parents=True)
            (root/'base.ff/maps/test.gsc').write_text('main() { maps\\old::run(); }')
            (root/'base.ff/maps/old.gsc').write_text('run() {}')
            (root/'patch.ff/maps/test.gsc').write_text('main() { maps\\absent::run(); }')
            report=audit.audit(root, ['base.ff','patch.ff'], ['maps/test.gsc'])
            self.assertEqual(report['resolved_scripts'][0]['zone'], 'patch.ff')
            self.assertEqual(report['missing_dependencies'], ['maps/absent.gsc'])
            self.assertEqual(report['overridden_paths'], ['maps/test.gsc'])
            self.assertEqual(report['schema_version'], 1)

    def test_casefold_dependency_lookup_and_cycles(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'base.ff/maps').mkdir(parents=True)
            (root/'base.ff/maps/A.gsc').write_text('#include maps\\b;\nmain() {}')
            (root/'base.ff/maps/B.gsc').write_text('#include maps\\A;\nmain() {}')
            report=audit.audit(root,['base.ff'],['maps/a.gsc'])
            self.assertEqual(len(report['resolved_scripts']),2)
            self.assertFalse(report['missing_dependencies'])

    def test_unsafe_path_and_dynamic_alias(self):
        with self.assertRaises(ValueError):
            audit.safe_script('../outside.gsc')
        with self.assertRaises(ValueError):
            audit.safe_script('/outside.gsc')
        report=audit.inspect_script('maps/test.gsc',b'main() { self PlaySound(level.alias); }')
        self.assertEqual(report['literal_sound_aliases'],[])
        self.assertEqual(report['dynamic_sound_calls'],1)

    def test_original_non_utf8_comment_bytes(self):
        report=audit.inspect_script('maps/test.gsc',b'// owner\x92s comment\nmain() {}\n')
        self.assertEqual(report['lexical_encoding'],'latin-1-byte-preserving')
        self.assertEqual(report['functions'],[{'name':'main','start_line':2,'end_line':2}])


if __name__ == '__main__':
    unittest.main()
