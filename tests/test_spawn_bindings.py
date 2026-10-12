"""Invented address graphs; original executable data is not a fixture."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from verify_spawn_bindings import validate_spawn_graph

class SpawnGraphTests(unittest.TestCase):
    def graph(self):
        return {name:0x900 for pair in [
            ('copy_static_call','copy_static'),('copy_dynamic_call','copy_dynamic'),
            ('physics_trace_call','bounds_trace'),('actor_trace_call','bounds_trace'),
            ('origin_set_call','origin_set'),('team_script_tail','team_set'),
            ('goal_script_call','goal_set'),('enemy_script_tail','enemy_set'),
            ('right_hand_call','animation_builder'),('left_hand_call','animation_builder')
        ] for name in pair}
    def test_consistent_graph(self):
        validate_spawn_graph(self.graph())
    def test_individually_valid_but_mismatched_native_functions_reject(self):
        for key in ('copy_static_call','copy_dynamic_call','physics_trace_call','actor_trace_call',
                    'origin_set_call','team_script_tail','goal_script_call','enemy_script_tail','right_hand_call','left_hand_call'):
            g=self.graph();g[key]+=16
            with self.subTest(key=key),self.assertRaisesRegex(ValueError,'disagrees'):validate_spawn_graph(g)
if __name__=='__main__':unittest.main()
