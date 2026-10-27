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
        names=self.listed('kZombieBuiltins')+self.listed('kAnimBuiltins')
        self.assertEqual(len(names),len(declared))
        self.assertEqual(len(set(names)),len(names))
        self.assertEqual(len(set(declared.values())),len(declared),'two builtins share one routine')
        for required in ('giveweapon','takeallweapons','switchtoweapon','givemaxammo','iprintlnbold','orientmode','animscripted','hide','show'):
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
        for role in ('Move','Idle','Death','RiseWalk','RiseRun','RiseSprint','Attack1','Attack2'):
            self.assertIn(role,roles)


if __name__=='__main__':unittest.main()
