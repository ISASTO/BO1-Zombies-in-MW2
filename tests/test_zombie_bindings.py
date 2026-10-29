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
        names=self.listed('kZombieBuiltins')+self.listed('kAnimBuiltins')+self.listed('kModelBuiltins')+self.listed('kSoundBuiltins')+self.listed('kWorldBuiltins')
        self.assertEqual(len(names),len(declared))
        self.assertEqual(len(set(names)),len(names))
        self.assertEqual(len(set(declared.values())),len(declared),'two builtins share one routine')
        for required in ('giveweapon','takeallweapons','switchtoweapon','givemaxammo','iprintlnbold','orientmode','animscripted','hide','show',
                         'setmodel','attach','detachall','playsound','musicplay','musicstop','spawn','delete','rotateto','movegravity',
                         'gettagorigin','stopsounds'):
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

    def test_model_zone_loads_before_the_level_zone(self):
        # Test41: assets are only found once their zone has loaded; the level
        # scripts precache the models before a zone queued after the level's.
        ai=(ROOT/'src'/'ai.cpp').read_text()
        hook=ai[ai.index('void LevelZoneLoadHook('):ai.index('bool InstallLevelZoneHook(')]
        order=[hook.index('if(addModels) zones[n++]={bo1models::kZoneName,info[0].allocFlags,0};'),
               hook.index('if(addSounds) zones[n++]={bo1sounds::kZoneName,info[0].allocFlags,0};'),
               hook.index('zones[n++]=info[0];'),hook.index('load(zones,n,sync);')]
        self.assertEqual(order,sorted(order))

    def test_builtin_and_thread_strings_are_pinned(self):
        # Test42: animscripted releases its own parameters (IncInParam) before
        # it pushes the borrowed notify name again; strings we pass must be
        # kept alive like script literals.
        ai=(ROOT/'src'/'ai.cpp').read_text()
        builtin=ai[ai.index('ScriptCallResult Builtin('):]
        self.assertLess(builtin.index('PinScriptStrings(args);'),builtin.index('CallScriptBuiltin('))
        setup=ai[ai.index('bool RunSetupFunction('):]
        self.assertLess(setup.index('PinScriptStrings(args);'),setup.index('CallScriptThread('))
        dress=ai[ai.index('void DressZombie('):]
        self.assertLess(dress.index('PinScriptStrings({ScriptArg::String(kHelmetSwapNote)});'),dress.index('CallScriptThread('))
        self.assertEqual(ai.count('CallScriptThread('),2)
        self.assertEqual(ai.count('CallScriptBuiltin('),1)
        self.assertIn('kScrAddStringType=7',self.bindings)

    def test_delta_flag_requires_a_delta_part(self):
        xanim=(ROOT/'include'/'bo1_xanim.hpp').read_text()
        self.assertIn('return a.delta && (a.trans.present || a.quat.present);',xanim)
        self.assertIn('(HasDelta(a)?mw2::kFlagDelta:0)',xanim)

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


class BO1SoundTests(unittest.TestCase):
    """Test43: the generated sound zone and how the game plays it."""
    def setUp(self):
        self.sounds=(ROOT/'include'/'bo1_sounds.hpp').read_text()
        self.build=(ROOT/'include'/'bo1_sound_build.hpp').read_text()
        self.source=(ROOT/'src'/'bo1_sounds.cpp').read_text()
        self.ai=(ROOT/'src'/'ai.cpp').read_text()

    def aliases(self):
        block=self.sounds[self.sounds.index('namespace alias {'):self.sounds.index('} // namespace alias')]
        found=dict(re.findall(r'const char\* (k\w+)="([^"]+)"',block))
        # Test45: arrays of aliases (the ambience one-shots) as kName[i].
        for name,items in re.findall(r'std::array<const char\*,\d+> (k\w+)=\{([^}]+)\}',block):
            for i,item in enumerate(re.findall(r'"([^"]+)"',items)):found[f'{name}[{i}]']=item
        return found

    def body(self,signature,end='\n}\n'):
        start=self.ai.index(signature)
        return self.ai[start:self.ai.index(end,start)]

    def test_runtime_aliases_are_built(self):
        built=re.findall(r'\{"([a-z0-9_]+)",mw2::kChannel\w+(,true)?\}',self.build)
        exact={name for name,prefix in built if not prefix};prefixes=[name for name,prefix in built if prefix]
        # Test45: lists written under another MW2 name ({"bo1 name",channel,false,"mw2 name",...}).
        exact|=set(re.findall(r'\{"[a-z0-9_]+",mw2::kChannel\w+,false,"([a-z0-9_]+)"',self.build))
        for key,name in self.aliases().items():
            if key=='kStepPrefix':
                self.assertIn(name,prefixes);continue
            self.assertTrue(name in exact or any(name.startswith(p) for p in prefixes),f'{name} is played but never built')
        runtime=re.search(r'kRuntimeAliases=\{([^}]+)\}',self.sounds)[1]
        listed=[n.strip().replace('alias::','') for n in runtime.split(',') if n.strip()]
        self.assertEqual(len(listed),int(re.search(r'array<const char\*,(\d+)> kRuntimeAliases',self.sounds)[1]))
        self.assertEqual(sorted(listed),sorted(k for k in self.aliases() if k!='kStepPrefix'))

    def test_zone_name_reaches_the_cached_file(self):
        zone=re.search(r'kZoneName="([^"]+)"',self.sounds)[1].replace('\\\\','\\')
        self.assertTrue(zone.startswith('..\\..\\bo1mw2\\'),zone)
        self.assertLess(len(zone),64)
        written=re.search(r'cacheDir\+L"\\\\([a-z0-9_]+)\.ff"',self.source)[1]
        self.assertTrue(zone.endswith('cache\\'+written),(zone,written))

    def test_played_aliases_are_known_constants(self):
        names=set(self.aliases())
        calls=[a for a in re.findall(r'PlayZombieSound\([^;]*?,([^;]*?)\)',self.ai) if not a.strip().startswith('const char*')]
        self.assertGreater(len(calls),15)
        for arg in calls:
            for key in re.findall(r'bo1sounds::alias::(k\w+)',arg): self.assertIn(key,names)
            if 'bo1sounds::alias::' not in arg: self.assertTrue(arg.strip() in ('alias','play') or arg.strip().startswith('present['),arg)

    def test_a_sound_needs_its_alias_and_an_active_level(self):
        play=self.body('bool PlayZombieSound(')
        self.assertLess(play.index('SoundAvailable(alias)'),play.index('kGSoundAliasIndexRva'))
        self.assertLess(play.index('sounds.active'),play.index('kGAddEventRva'))
        self.assertIn('kEventSoundAlias',play)

    def test_playsound_hook_owns_nothing_across_script_errors(self):
        # Scr_GetString raises script errors with longjmp: no destructors may be live.
        for signature in ('void PlaySoundHook(','bool ZombieSoundOverride('):
            body=self.body(signature)
            for owner in ('std::string','lock_guard','unique_lock','std::vector','std::unique_ptr'):
                self.assertNotIn(owner,body,f'{signature} {owner}')
        hook=self.body('void PlaySoundHook(')
        self.assertLess(hook.index('kScrGetNumParamRva'),hook.index('kScrGetStringRva'))
        self.assertIn('reinterpret_cast<ImplFn>(base+kPlaySoundImplRva)(entref,kEventSoundAlias);',hook)

    def test_only_zombies_lose_soldier_voices(self):
        override=self.body('bool ZombieSoundOverride(')
        self.assertIn('ClassifySoldierSound(alias)',override)
        classify=self.sounds[self.sounds.index('inline SoldierSound ClassifySoldierSound('):]
        for literal in ('"step_run_"','"generic_"','"gear_rattle"','"bodyfall_"'):self.assertIn(literal,classify)
        hook=self.body('void PlaySoundHook(')
        self.assertIn('sounds.zombieUntil[number]',hook)
        self.assertIn('if(!z.dog) sounds.zombieUntil[',self.ai)



class Test44Tests(unittest.TestCase):
    """Test44: the loaded_sound pool, walkable spawn points, melee reach and anims."""
    def setUp(self):
        self.ai=(ROOT/'src'/'ai.cpp').read_text()
        self.bindings=(ROOT/'include'/'zombie_bindings.hpp').read_text()

    def body(self,signature,end='\n}\n'):
        start=self.ai.index(signature)
        return self.ai[start:self.ai.index(end,start)]

    def test_pool_is_enlarged_in_dllmain_before_the_game_runs(self):
        proxy=(ROOT/'src'/'proxy.cpp').read_text()
        attach=proxy[proxy.index('if (reason == DLL_PROCESS_ATTACH)'):proxy.index('} else if (reason == DLL_PROCESS_DETACH)')]
        self.assertIn('mod::assetpools::Expand();',attach)
        source=(ROOT/'src'/'asset_pools.cpp').read_text()
        expand=source[source.index('bool Expand()'):source.index('const Status& GetStatus()')]
        # Every check runs before the first write.
        self.assertLess(expand.index('Refusal(host)'),expand.index('VirtualAlloc'))
        self.assertLess(expand.index('VirtualAlloc'),expand.index('std::memcpy'))
        refusal=source[source.index('const char* Refusal('):source.index('bool Expand()')]
        for check in ('POOL_SIZE_DIFFERS','POOL_POINTER_DIFFERS','POOL_INIT_DIFFERS','POOL_ALLOC_DIFFERS','POOL_FREE_DIFFERS',
                      'DB_INIT_DIFFERS','DB_INIT_CALL_DIFFERS','POOLS_ALREADY_LINKED','ASSET_NAME_DIFFERS'):
            self.assertIn(check,refusal)
        self.assertEqual(int(re.search(r'kLoadedSoundExpandedCount=(\d+)',self.bindings)[1]),4096)

    def test_spawns_prefer_walkable_points(self):
        spawn=self.body('bool SpawnZombie(')
        self.assertLess(spawn.index('NavBand('),spawn.index('SpawnPointBand('))
        self.assertIn('ZombieTemplate(t)',spawn)
        # Too few reachable spawners: walkable stand nodes join the band.
        self.assertLess(spawn.index('NavNodeBand('),spawn.index('SpawnPointBand('))
        self.assertIn('nav::StandNode(node)',self.body('std::vector<std::pair<AiEntity,float>> NavNodeBand('))
        service=self.body('void ServiceZombies(const Snapshot& s,const AiScope& scope,int time) {')
        self.assertLess(service.index('ServiceNav('),service.index('SpawnZombie('))
        nav=self.body('void ServiceNav(')
        self.assertIn('>=5000',nav)                  # links re-read: doors open and close
        self.assertIn('nav::DistancesTo(',nav)

    def test_drones_never_become_zombies(self):
        template=self.body('bool ZombieTemplate(')
        for word in ('drone','riotshield','civilian'):
            self.assertIn('"'+word+'"',template)

    def test_a_swing_starts_only_in_reach(self):
        melee=self.body('void ServiceMelee(')
        admission=melee.index('distance>bo1::kMeleeRangeSquared')
        self.assertLess(admission,melee.index('CanMeleePlayer('))
        self.assertIn('kMeleeHeightTolerance',melee[admission-200:admission+200])
        self.assertNotIn('kLongChargeRangeSquared',melee)

    def test_zombies_skip_turn_anims(self):
        self.assertIn('{"maps/_utility","disable_turnanims",0}',self.bindings)
        self.assertIn('SetupFunction::DisableTurnAnims',self.body('void RunZombieSetup('))

    def test_mw2_scripts_never_play_the_bo1_only_leaves_when_available(self):
        # Optional local check against the owner's extracted MW2 animscripts.
        import os
        path=os.environ.get('MW2_SCRIPTS')
        if not path:self.skipTest('MW2_SCRIPTS not set (local extracted animscripts)')
        scripts=Path(path)/'animscripts'
        utility=(Path(path)/'maps'/'_utility.gsc').read_text(errors='replace')
        self.assertIn('self.noTurnAnims = true;',utility[utility.index('\ndisable_turnAnims()'):][:200])
        move=(scripts/'move.gsc').read_text(errors='replace')
        self.assertIn('isdefined( self.noTurnAnims )',move)
        slots=re.findall(r'\{"([a-z0-9_]+)","(ai_zombie_[a-z0-9_]+)",AnimRole::(\w+)\}',self.ai)
        special=[mw2 for mw2,_,role in slots if role not in ('Move','Idle','Death')]
        # The scripts a zombie runs while it walks, idles, reacts or dies.
        for name in ('move.gsc','run.gsc','walk.gsc','stop.gsc','pain.gsc','death.gsc','cqb.gsc'):
            text=(scripts/name).read_text(errors='replace').lower()
            for leaf in special:
                self.assertNotIn('%'+leaf,text,f'{name} plays {leaf}')
        reactions=(scripts/'reactions.gsc').read_text(errors='replace').lower()
        for leaf in ('run_react_duck','run_react_flinch'):
            self.assertNotIn(leaf,special)
            self.assertIn('%'+leaf,reactions)


class Test45Tests(unittest.TestCase):
    """Test45: no soldier sounds or mission music, ambience and announcer,
    powerup models, BO1 HUD, dismemberment, auto logs and the end of a game."""
    def setUp(self):
        self.ai=(ROOT/'src'/'ai.cpp').read_text()
        self.bindings=(ROOT/'include'/'zombie_bindings.hpp').read_text()
        self.build=(ROOT/'include'/'bo1_sound_build.hpp').read_text()

    def body(self,signature,end='\n}\n'):
        start=self.ai.index(signature)
        return self.ai[start:self.ai.index(end,start)]

    def test_mission_music_is_blocked_only_on_zombies_levels(self):
        prepare=self.body('void PrepareLevelScripts() {')
        self.assertLess(prepare.index('if(!launch || !prep.bindings) return;'),prepare.index('SetMusicBlock(true)'))
        self.assertIn('SetMusicBlock(false);',self.body('void RestoreLevelPrep('))
        block=self.body('bool SetMusicBlock(')
        self.assertIn('observed!=kMusicPlayPrologue',block)
        self.assertIn('ExchangeCode(musicEdit,block)',block)
        self.assertIn('kBuiltinMusicStop',self.body('void BeginZombies('))
        # The map's ambience (ambientplay) is a different builtin and stays.
        self.assertNotIn('ambientplay',self.bindings)

    def test_every_soldier_sound_on_a_zombie_is_dropped_or_replaced(self):
        override=self.body('bool ZombieSoundOverride(')
        self.assertIn('case bo1sounds::SoldierSound::Keep: return false;',override)
        self.assertEqual(override.count('return false'),1)

    def test_ambience_and_announcer_are_built_and_played(self):
        self.assertIn('"bo1_zombie_ambience_bed",true',self.build)
        spec=self.build[self.build.index('inline bool BuildSoundSpec('):]
        self.assertIn('detail::AddBank(announcer,localized.raw,kAnnouncerLists',spec)
        grab=self.body('void GrabPowerup(')
        for key in ('kAnnMaxAmmo','kAnnInstaKill','kAnnNuke','kAnnDoublePoints'):self.assertIn(key,grab)
        service=self.body('void ServiceZombies(const Snapshot& s,const AiScope& scope,int time) {')
        self.assertIn('ServiceAmbience(time);',service)
        self.assertIn('kAnnDogStart',service)

    def test_zombies_info_is_logged_after_rounds_and_at_the_end(self):
        service=self.body('void ServiceZombies(const Snapshot& s,const AiScope& scope,int time) {')
        self.assertIn('ZOMBIE_AUTO_LOG reason=ROUND_',service)
        self.assertIn('LogZombieGame();',service)
        reset=self.body('void ResetZombieGame(const char* reason,bool deleteActors) {')
        self.assertLess(reset.index('LogZombieGame();'),reset.index('zg.running=false;'))

    def test_game_over_returns_to_the_main_menu_and_leaving_ends_zombies(self):
        game=(ROOT/'include'/'zombie_game.hpp').read_text()
        self.assertIn('bool autoRestart=false;',game)
        tick=self.body('void TickZombieRelaunch(const Snapshot& s) {')
        self.assertIn('LevelState::NoLevel',tick)
        self.assertIn('ResetZombieGame("LEFT_LEVEL",false)',tick)
        self.assertIn('kDisconnectCommand',tick)
        self.assertIn('kDisconnectCommand="disconnect\\n"',self.bindings)
        reset=self.body('void ResetZombieGame(const char* reason,bool deleteActors) {')
        self.assertIn('zg.menuAt=GetTickCount64()+kGameOverMenuMilliseconds',reset)
        # Builtins only while the level is still there.
        self.assertIn('if(levelAlive) {StopPlayerSounds();RemoveDropModels();}',reset)

    def test_world_models_are_made_by_the_service_only(self):
        # QueueDrop and the damage hook run inside G_Damage: no spawn, delete or setmodel there.
        for signature in ('void QueueDrop(','bool ZombieDamage(','void QueueGib('):
            body=self.body(signature)
            for call in ('SpawnScriptModel(','DeleteWorldModel(','Builtin('):self.assertNotIn(call,body,f'{signature} {call}')
        self.assertIn('drop.wantModel=true;',self.body('void QueueDrop('))
        self.assertIn('SpawnDropModel(drop,time)',self.body('void ServiceDropModel('))
        self.assertIn('ServiceGibs(time);',self.body('void ServiceZombies(const Snapshot& s,const AiScope& scope,int time) {'))

    def test_head_gib_never_detaches_a_missing_model(self):
        apply=self.body('bool ApplyGib(')
        self.assertNotIn('kBuiltinDetach,',apply)
        self.assertIn('kBuiltinDetachAll',apply)
        # Tags are read before the body changes.
        self.assertLess(apply.index('kBuiltinGetTagOrigin'),apply.index('kBuiltinSetModel'))

    def test_spawn_diff_leaves_room_for_the_game(self):
        spawn=self.body('WorldModel SpawnScriptModel(')
        self.assertIn('used+96>',spawn)
        self.assertIn('found!=1',spawn)
