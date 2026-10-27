"""Test38/39 binding-table consistency, with no original executable data."""
import re
import sys
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import run_script_call_probe as probe


class ZombieBindingTableTests(unittest.TestCase):
    def setUp(self):
        self.text=(ROOT/'include'/'zombie_bindings.hpp').read_text()

    def builtins(self):
        return {m[1]:int(m[2],16) for m in re.finditer(r'kBuiltin\w+\{"(\w+)",(0x[0-9a-f]+)\}',self.text)}

    def listed(self,table):
        return [n.strip() for n in re.search(table+r'=\{([^}]+)\}',self.text)[1].split(',') if n.strip()]

    def test_builtin_list_matches_declarations(self):
        declared=self.builtins()
        names=self.listed('kZombieBuiltins')+self.listed('kAnimBuiltins')+self.listed('kModelBuiltins')
        self.assertEqual(len(names),len(declared))
        self.assertEqual(len(set(names)),len(names))
        self.assertEqual(len(set(declared.values())),len(declared),'two builtins share one routine')
        for required in ('giveweapon','takeallweapons','switchtoweapon','givemaxammo','iprintlnbold','orientmode','animscripted','hide','show',
                         'setmodel','attach','detachall'):
            self.assertIn(required,declared)

    def test_probe_gate_covers_the_value_pushes(self):
        for name in ('kScrAddIntRva','kScrAddFloatRva','kScrAddStringRva','kScrAddVectorRva','kScrAddEntityNumRva','kScrAddUndefinedRva','kScrIncInParamRva'):
            rva=int(re.search(name+r'=(0x[0-9a-f]+)',self.text)[1],16)
            self.assertIn(rva,probe.EXPECTED,f'{name} is bound without a native prefix check')
        self.assertIn(self.builtins()['min'],probe.EXPECTED)

    def test_damage_enums_follow_the_retail_tables(self):
        self.assertIn('kModMelee=8',self.text)
        self.assertIn('kHitHead=2',self.text)
        self.assertIn('kHitShield=19',self.text)


class SpecOpsMapTests(unittest.TestCase):
    def test_spec_ops_maps_are_safe_devmap_names(self):
        text=(ROOT/'include'/'zombie_game.hpp').read_text()
        names=re.findall(r'\{"([a-z0-9_]+)","[^"]+"\}',text)
        self.assertEqual(len(names),19)
        self.assertEqual(len(set(names)),19)
        self.assertTrue(all(n.startswith('so_') and len(n)<=32 for n in names))
        # Co-op-only (AC-130, helicopter gunner) and vehicle races are excluded.
        for excluded in ('so_ac130_co_hunted','so_chopper_invasion','so_snowrace1_cliffhanger','so_snowrace2_cliffhanger'):
            self.assertNotIn(excluded,names)
        self.assertIn('so_killspree_trainer',names);self.assertIn('so_showers_gulag',names)

    def test_spec_ops_end_functions_are_the_mission_over_paths(self):
        text=(ROOT/'include'/'zombie_bindings.hpp').read_text()
        refs=re.findall(r'\{"(maps/[a-z_]+)","([a-z_]+)"\}',text)
        self.assertEqual(refs,[('maps/_specialops','fade_challenge_out'),
            ('maps/_specialops_code','specialops_mission_over_setup'),('maps/_specialops_code','failure_summary_display')])


class AnimSlotTests(unittest.TestCase):
    def test_slots_use_wanted_bo1_anims_once_per_mw2_anim(self):
        ai=(ROOT/'src'/'ai.cpp').read_text()
        slots=re.findall(r'\{"([a-z0-9_]+)","(ai_zombie_[a-z0-9_]+)",AnimRole::(\w+)\}',ai)
        self.assertGreaterEqual(len(slots),30)
        mw2=[m for m,_,_ in slots]
        self.assertEqual(len(mw2),len(set(mw2)),'an MW2 anim is replaced twice')
        wanted=set(re.findall(r'"(ai_zombie_[a-z0-9_]+)"',(ROOT/'src'/'bo1_assets.cpp').read_text()))
        self.assertTrue({b for _,b,_ in slots}<=wanted,'a slot uses an anim the loader does not carve')
        roles={r for _,_,r in slots}
        enum=re.search(r'enum class AnimRole : int \{([^}]+)\}',ai)[1]
        declared=[r.split('//')[0].strip() for r in re.sub(r'//[^\n]*','',enum).split(',')]
        declared=[r for r in declared if r and r!='Count']
        # Every role has a slot, and every BO1-only role has exactly one donor.
        self.assertEqual(set(declared),roles)
        for role in declared[3:]:
            self.assertEqual(sum(1 for _,_,r in slots if r==role),1,role)
        # The melee tables are BO1's _zombie_melee (6), walk (4) and run (3) sets.
        for table,size in (('kMeleeAnims',6),('kWalkMeleeAnims',4),('kRunMeleeAnims',3)):
            listed=re.search(table+r'\[\]=\{([^}]+)\}',ai)[1].split(',')
            self.assertEqual(len(listed),size,table)

    def test_mw2_slot_anims_exist_when_local_zone_is_available(self):
        # Optional local check: names verified against the owner's common.ff
        # (never committed) when MW2_COMMON_DEC points at a decompressed copy.
        import os
        path=os.environ.get('MW2_COMMON_DEC')
        if not path:self.skipTest('MW2_COMMON_DEC not set')
        raw=Path(path).read_bytes()
        ai=(ROOT/'src'/'ai.cpp').read_text()
        for mw2,_,_ in re.findall(r'\{"([a-z0-9_]+)","(ai_zombie_[a-z0-9_]+)",AnimRole::(\w+)\}',ai):
            hits=[m.start()+1 for m in re.finditer(rb'[\x00\xff]'+re.escape(mw2.encode())+rb'\0',raw)]
            self.assertTrue(any(raw[h-136:h-128]==b'\xff'*8 for h in hits),f'{mw2} is not an XAnimParts in common.ff')


class SetupFunctionTests(unittest.TestCase):
    def test_setup_functions_match_the_enum(self):
        text=(ROOT/'include'/'zombie_bindings.hpp').read_text()
        enum=re.search(r'enum class SetupFunction : int \{([^}]+)\}',text)[1]
        names=[n.strip() for n in enum.split(',') if n.strip() and n.strip()!='Count']
        funcs=re.findall(r'\{"(maps/_utility)","([a-z_]+)",([0-9])\}',text)
        self.assertEqual(len(names),len(funcs))
        self.assertEqual(len({f for _,f,_ in funcs}),len(funcs))
        self.assertIn(('maps/_utility','gun_remove','0'),funcs)
        self.assertIn(('maps/_utility','set_battlechatter','1'),funcs)
        self.assertEqual([f for _,f,p in funcs if p!='0'],['set_battlechatter'])


if __name__=='__main__':unittest.main()


class ModelImportTests(unittest.TestCase):
    """Test40: names shared by the zone writer, the DLL and the bindings."""
    def setUp(self):
        self.models=(ROOT/'include'/'bo1_models.hpp').read_text()
        self.model=(ROOT/'include'/'bo1_model.hpp').read_text()
        self.source=(ROOT/'src'/'bo1_models.cpp').read_text()
        self.bindings=(ROOT/'include'/'zombie_bindings.hpp').read_text()

    def names(self,text,table):
        return re.findall(r'"([^"]+)"',re.search(table+r'=\{([^}]+)\}',text)[1])

    def test_aliases_name_models_the_zone_contains(self):
        bodies=self.names(self.model,'kBodyModels');heads=self.names(self.model,'kHeadModels')
        self.assertEqual(sorted(set(self.names(self.models,'kBodyAlias'))),sorted(bodies))
        self.assertEqual(self.names(self.models,'kHeadAlias'),heads)
        # BO1 c_ger_honorguard_zomb_bodyalias: body1 once, body2 three times.
        self.assertEqual(self.names(self.models,'kBodyAlias').count(bodies[1]),3)

    def test_zone_name_reaches_the_cached_file(self):
        zone=re.search(r'kZoneName="([^"]+)"',self.models)[1].replace('\\\\','\\')
        self.assertTrue(zone.startswith('..\\..\\bo1mw2\\'),zone)
        self.assertLess(len(zone),64,'zone names are copied into 64-byte slots')
        written=re.search(r'cacheDir\+L"\\\\([a-z0-9_]+)\.ff"',self.source)[1]
        self.assertTrue(zone.endswith('cache\\'+written),(zone,written))

    def test_level_zone_flags_and_helmet_note(self):
        self.assertIn('kLevelZoneAllocFlags=4',self.bindings)
        self.assertIn('"swap taghelmet to tagleft"',self.bindings)
        self.assertIn('kHelmetSwapFunction{"animscripts/shared","handlenotetrack"}',self.bindings)

    def test_helmet_note_exists_in_local_mw2_scripts(self):
        import os
        scripts=os.environ.get('MW2_SCRIPTS')
        if not scripts: self.skipTest('MW2_SCRIPTS not set (local extracted animscripts)')
        shared=(Path(scripts)/'animscripts'/'shared.gsc').read_text(errors='replace')
        self.assertRegex(shared,r'(?i)\nHandleNoteTrack\(\s*note')
        case=shared[shared.index('case "swap taghelmet to tagleft":'):]
        self.assertIn('self.hatModel = undefined;',case[:400])
