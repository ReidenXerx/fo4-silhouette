"""Named people of popular quest mods (S-100): a fixed body each, instead of BodyGen's roll (owner-approved request,
DNater98 on Nexus, 2026-11-11: "Tales of the Commonwealth, America Rising 2 and the like").

S-66 and S-93 drew every body from the person's story. For these mods' people the body comes from their ROLE -- a
raider, a ballplayer of the baseball cult, an Enclave officer, a Paradise settler -- and a small, fixed variation of
the role's values made from the person's name, so no two of them share a body and the same person always has the same
one. A line whose plugin is not installed costs nothing: LooksMenu skips it (S-93). Records and sexes were read from
the plugins themselves (characters.py --check reads them again); the mods' own people S-93 already gave a body
(Lola, Cherry, Lana Lafferty, Sonja, Maisie, Penelope, Switch, Spitball, Rabbit; Doctor Kane, Quartermaster
Barratt, Hailstorm, Doctor Armstrong) keep it.

Children, robots, corpses, generic ranks (Enclave Private, Fury Company Trooper), voices (Intercom, Narrator) and
people named only by a job (Settler, Bartender, Auctioneer) are left out.
"""
import hashlib

from archetypes import CAP_F, LIMIT

TOTC, AR2 = '3DNPC_FO4.esp', 'AmericaRising2.esm'

# role -> (sex, what the role looks like, slider values in percent)
ROLES = {
    'settler_f': ('female', 'a working woman of the wasteland: ordinary, a little soft at the middle',
                  {'ChubbyWaist': 70, 'Belly': 20, 'Breasts': 10, 'BreastGravity2': 25, 'Thighs': 15, 'ChubbyArms': 10}),
    'raider_f': ('female', 'a gang woman: lean and hard, nothing spare on her',
                 {'MuscularArms': 30, 'MuscularLegs': 30, 'ChubbyWaist': 55, 'WideWaistLine': 35, 'BreastsSmall': 25,
                  'ButtSmall': 15, 'LegsThin': 15}),
    'soldier_f': ('female', 'a soldier: athletic, strong legs and shoulders, a trained body',
                  {'MuscularArms': 40, 'MuscularLegs': 45, 'Back': 20, 'ShoulderWidth': 20, 'ChubbyWaist': 50,
                   'WideWaistLine': 30, 'BreastsSmall': 20, 'MuscularButt': 40}),
    'leader_f': ('female', 'a woman in charge: trim, upright and severe',
                 {'ChubbyWaist': 60, 'WideWaistLine': 45, 'BreastsSmall': 25, 'BreastPerkiness': 15, 'ButtSmall': 20,
                  'Back': 15, 'MuscularArms': 15}),
    'working_f': ('female', 'a woman who works the clubs and the night: a real hourglass',
                  {'Breasts': 35, 'Hips': 20, 'Butt': 25, 'RoundAss': 30, 'ChubbyWaist': 25, 'Thighs': 15,
                   'BreastPerkiness': 15}),
    'elder_f': ('female', 'an older woman who has fed everyone around her: heavy, soft and sagging',
                {'Belly': 55, 'BigBelly': 30, 'ChubbyWaist': 85, 'WideWaistLine': 60, 'Breasts': 30, 'BreastGravity2': 60,
                 'ChubbyArms': 45, 'ChubbyLegs': 35, 'Thighs': 35, 'Hips': 25}),
    'scholar_f': ('female', 'a doctor or scholar: slim and soft, a life indoors',
                  {'ChubbyWaist': 70, 'WideWaistLine': 40, 'BreastsSmall': 20, 'BreastGravity2': 20, 'ButtSmall': 20,
                   'LegsThin': 15, 'Belly': 10}),
    'hippie_f': ('female', 'a free spirit: soft, natural, unbound curves',
                 {'ChubbyWaist': 65, 'Belly': 25, 'Breasts': 20, 'BreastGravity2': 35, 'Hips': 20, 'Thighs': 25,
                  'ChubbyArms': 15}),
    'athlete_f': ('female', 'a sportswoman of the cult of the old games: strong legs, a hard behind',
                  {'MuscularLegs': 45, 'MuscularButt': 45, 'CalfSize': 30, 'MuscularArms': 25, 'ChubbyWaist': 45,
                   'Butt': 20, 'BreastsSmall': 15}),
    'ascetic_f': ('female', 'a Child of Atom: thin from fasting and faith',
                  {'LegsThin': 50, 'Arms': 45, 'BreastsSmall': 25, 'ButtSmall': 45, 'ChubbyWaist': 45, 'SlimThighs': 40}),
    'ghoul_f': ('female', 'a ghoul woman: wasted thin under the ruined skin',
                {'LegsThin': 55, 'Arms': 55, 'BreastsSmall': 25, 'ButtSmall': 50, 'ChubbyWaist': 45, 'SlimThighs': 40}),
    'settler_m': ('male', 'a working man of the wasteland: an ordinary, average body',
                  {'BTStomachFat': 40, 'BTChestSmoothing': 30, 'BTChubbyArm': 15, 'BTShoulders': 10}),
    'raider_m': ('male', 'a gang man: wiry and hard, ribs and sinew',
                 {'BTAbDefinition': 35, 'BTThinArm': 25, 'ThinThigh': 25, 'BTNegStomach': 35, 'BTTraps': 30,
                  'BTBiceps': 20}),
    'brute_m': ('male', 'the muscle: a big, thick heavy who blocks doors',
                {'TigerSanBBMale': 35, 'BTShoulders': 50, 'BTTraps': 50, 'BTBiceps': 45, 'BTPectorals': 35,
                 'BTStomachFat': 35, 'BTBack': 40}),
    'soldier_m': ('male', 'a soldier: drilled and solid',
                  {'BTShoulders': 40, 'BTTraps': 40, 'BTPectorals': 30, 'BTBiceps': 35, 'BTAbDefinition': 25,
                   'BTThigh': 25}),
    'officer_m': ('male', 'a man in command: fit, older, kept to regulation',
                  {'BTShoulders': 25, 'BTPectorals': 20, 'BTAbDefinition': 15, 'BTStomachFat': 35, 'BTBiceps': 15}),
    'heavy_m': ('male', 'a big man who eats well: heavy all over, the weight spread, no pot out front',
                {'BTStomachFat': 70, 'BTBodyFatv2': 35, 'BTChestSmoothing': 55, 'BTChubbyArm': 40, 'BTChubbyLeg': 30,
                 'Hips': 30}),
    'thin_m': ('male', 'a thin man -- a doctor, an addict, a believer: narrow, round-shouldered',
               {'BTThinArm': 50, 'ThinThigh': 45, 'BTNegShoulder': 40, 'BTNegPecMuscle': 45, 'BTNegChestWidth': 35,
                'BTStomachFat': 15}),
    'elder_m': ('male', 'an older man: soft in the middle, the muscle gone',
                {'BTStomachFat': 55, 'BTChestSmoothing': 50, 'BTNegPecMuscle': 30, 'BTThinArm': 30, 'BTNegShoulder': 25,
                 'Hips': 20}),
    'athlete_m': ('male', 'a ballplayer of the cult of the old games: strong legs and shoulders',
                  {'BTThigh': 40, 'BTCalves': 35, 'BTShoulders': 35, 'BTBiceps': 30, 'BTAbDefinition': 30,
                   'BTPectorals': 25, 'BTStomachFat': 20}),
    'pretty_m': ('male', 'a man who sells his looks: lean and defined',
                 {'BTAbDefinition': 45, 'BTWaist-In': 45, 'BTPectorals': 25, 'BTBiceps': 20, 'BTNegStomach': 35}),
    'hippie_m': ('male', 'a free spirit: lanky and soft',
                 {'BTThinArm': 30, 'ThinThigh': 25, 'BTStomachFat': 35, 'BTChestSmoothing': 35, 'BTNegShoulder': 20}),
    'ghoul_m': ('male', 'a ghoul man: wasted thin under the ruined skin',
                {'BTNegStomach': 55, 'BTThinArm': 55, 'ThinThigh': 50, 'BTNegPecMuscle': 50, 'BTNegShoulder': 30,
                 'BTTHinCalf': 40}),
}

SPREAD = 12  # each value moves at most this many points, by the person's name
# The women's caps: S-96's, and the bust floor (owner poll 2026-09-24: BreastsSmall at most 30, test_pool.BustFloor).
CAPS_F = {**CAP_F, 'BreastsSmall': 30}


def vary(name, sex, values):
    """The role's values, each moved by -SPREAD..+SPREAD from a hash of the name and the slider: fixed per person,
    inside 0..100 and, for women, under CAPS_F (S-96 and the bust floor)."""
    out = {}
    for slider, v in values.items():
        h = hashlib.sha1(f'{name}|{slider}'.encode('utf-8')).digest()
        d = h[0] % (2 * SPREAD + 1) - SPREAD
        hi = min(LIMIT[1], CAPS_F.get(slider, LIMIT[1])) if sex == 'female' else LIMIT[1]
        out[slider] = max(LIMIT[0], min(hi, v + d))
    return out


def characters():
    """characters.py's rows: (name, sex, [(plugin, editor id, local id)], vibe, values)."""
    rows = []
    for name, role, forms in PEOPLE:
        sex, vibe, values = ROLES[role]
        mod = 'Tales from the Commonwealth' if forms[0][0] == TOTC else 'America Rising 2'
        rows.append((name, sex, forms, f'{mod}, {vibe}', vary(name, sex, values)))
    return rows


# name, role, [(plugin, editor id, local id)]
PEOPLE = [
    ('Audrey', 'ghoul_f', [(TOTC, 'f3DNPC_CompanionAudrey', '001E1C')]),
    ('R4-04', 'soldier_f', [(TOTC, 'f3DNPC_CompanionR404', '004356'), (TOTC, 'f3DNPC_R404clone', '027A0E')]),
    ('Niah', 'leader_f', [(TOTC, 'f3DNPC_NiahNPC', '014B06')]),
    ('Anna Lee', 'settler_f', [(TOTC, 'f3DNPC_AudreyTzen_AnnaLee', '0124BC')]),
    ('Annie', 'settler_f', [(TOTC, 'f3DNPC_Bunker_Annie', '0030D6')]),
    ('Aunt Patty', 'elder_f', [(TOTC, 'f3DNPC_Jacks_AuntPatty', '034AAA')]),
    ('Beth', 'settler_f', [(TOTC, 'f3DNPC_Paradise_SettlerA01', '00A098')]),
    ('Cameron', 'settler_f', [(TOTC, 'f3DNPC_Photo_Cameron', '002ABF')]),
    ('Carmen', 'raider_f', [(TOTC, 'f3DNPC_AudreyTzen_Carmen', '01897F')]),
    ('Cecylia', 'settler_f', [(TOTC, 'f3DNPC_Immigrant_Cecylia', '00D3C5')]),
    ('Clarissa', 'settler_f', [(TOTC, 'f3DNPC_Patriot_Clarissa', '00FEC6')]),
    ('Commie', 'soldier_f', [(TOTC, 'f3DNPC_Gunners_Commie', '0362C8')]),
    ('Crash', 'raider_f', [(TOTC, 'f3DNPC_EastBoston_Crash', '003FD2')]),
    ('Doctor Singh', 'scholar_f', [(TOTC, 'f3DNPC_Paradise_DoctorSingh', '006B2F')]),
    ('Emily', 'ascetic_f', [(TOTC, 'f3DNPC_Children_Emily', '010E34'), (TOTC, 'f3DNPC_Children_EmilyPost', '00E637'), (TOTC, 'f3DNPC_Children_EmilyGhoul', '0141AB')]),
    ('Fast Pitch Fannie', 'athlete_f', [(TOTC, 'f3DNPC_Baseball_Cult02', '00EC5E')]),
    ('Gran', 'elder_f', [(TOTC, 'f3DNPC_RaiderGran', '0029DD')]),
    ('Holly Winters', 'settler_f', [(TOTC, 'f3DNPC_MemoryLady', '00F345')]),
    ('Jacqueline', 'settler_f', [(TOTC, 'f3DNPC_FaceDoctorWife', '01CE01')]),
    ('Jane', 'working_f', [(TOTC, 'f3DNPC_Brothel_JaneDoe', '00270F')]),
    ('Knight Casilla', 'ghoul_f', [(TOTC, 'f3DNPC_BoSGhoul_KnightCasilla', '00B9FB')]),
    ('Lynn', 'settler_f', [(TOTC, 'f3DNPC_EastBoston_Lynn', '0030BA')]),
    ('Mama Mabel', 'elder_f', [(TOTC, 'f3DNPC_Karaoke_MamaMabel', '009702')]),
    ('Marion Taylor', 'settler_f', [(TOTC, 'f3DNPC_MailmanDaughter', '001F95')]),
    ('Molly', 'settler_f', [(TOTC, 'f3DNPC_Molly', '0047C2')]),
    ('Paladin Brend', 'soldier_f', [(TOTC, 'f3DNPC_BosGuard03', '03EF28')]),
    ('Rhonda', 'settler_f', [(TOTC, 'f3DNPC_RhondaNPC', '010309')]),
    ('Sadie', 'settler_f', [(TOTC, 'f3DNPC_Sadie', '0047B9')]),
    ('Sally', 'settler_f', [(TOTC, 'f3DNPC_BusLady', '00E68C')]),
    ('Sarah', 'settler_f', [(TOTC, 'f3DNPC_Paradise_SettlerA06', '012B5A')]),
    ('Smokescreen', 'raider_f', [(TOTC, 'f3DNPC_AtomCat_Smoke', '00357A')]),
    ('Sophie', 'working_f', [(TOTC, 'f3DNPC_Jewel_Sophie', '02CEE7')]),
    ('Starchild', 'hippie_f', [(TOTC, 'f3DNPC_Hippies_Starchild', '002B61')]),
    ('Susan', 'settler_f', [(TOTC, 'f3DNPC_CastleSusan', '0104EF')]),
    ('Washout', 'athlete_f', [(TOTC, 'f3DNPC_Bowling_Washout', '009924')]),
    ('Birdie', 'soldier_m', [(TOTC, 'f3DNPC_CompanionBirdie', '004357')]),
    ('John Castle', 'brute_m', [(TOTC, 'f3DNPC_NPCCastle', '003CB6'), (TOTC, 'f3DNPC_NPCCastleClone01', '003D17'), (TOTC, 'f3DNPC_NPCCastleClone03', '003D1E')]),
    ('Alex', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerA04', '00A09B')]),
    ('Anchorage', 'ghoul_m', [(TOTC, 'f3DNPC_Gunners_Anchorage', '0362C9')]),
    ('Arran', 'ghoul_m', [(TOTC, 'f3DNPC_Immigrant_Arran', '00D2DA')]),
    ('Arthur Dean', 'settler_m', [(TOTC, 'f3DNPC_Jacks_ArthurDean', '034A6B')]),
    ('Bad-X', 'raider_m', [(TOTC, 'f3DNPC_RaiderBadX', '0029DB')]),
    ('Black Eye Bobby', 'raider_m', [(TOTC, 'f3DNPC_RaiderBlackEyeBobby', '0029DA')]),
    ('Bobby Crane', 'settler_m', [(TOTC, 'f3DNPC_Jacks_BobbyCrane', '034AA9')]),
    ('Boxcar', 'brute_m', [(TOTC, 'f3DNPC_CastleBoxcar', '016EA3')]),
    ('Brennan McConnell', 'brute_m', [(TOTC, 'f3DNPC_Jewel_Brennan', '02A843')]),
    ('Brick', 'brute_m', [(TOTC, 'f3DNPC_Gunners_Brick', '0362C6')]),
    ('Brother Jacob', 'ghoul_m', [(TOTC, 'f3DNPC_Children_Jacob', '010E36')]),
    ('Bruce', 'athlete_m', [(TOTC, 'f3DNPC_BaseballBruce', '0140D4')]),
    ('Cain', 'ghoul_m', [(TOTC, 'f3DNPC_RaiderCain', '003B33')]),
    ('Catcher', 'brute_m', [(TOTC, 'f3DNPC_Baseball_Cult07', '00EC69')]),
    ('Chase', 'pretty_m', [(TOTC, 'f3DNPC_Prostitute04M', '002694')]),
    ('Coach', 'heavy_m', [(TOTC, 'f3DNPC_Baseball_Cult01', '00EC34')]),
    ('Cosmos', 'hippie_m', [(TOTC, 'f3DNPC_Hippies_Cosmos', '002980')]),
    ('Craig', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerA05', '00A09C')]),
    ('Cueball', 'raider_m', [(TOTC, 'f3DNPC_Cueball', '00485C')]),
    ('Denton', 'raider_m', [(TOTC, 'f3DNPC_Treasure_Hunter', '003A20')]),
    ('Derrick McConnell', 'raider_m', [(TOTC, 'f3DNPC_Jewel_Derrick', '02A845')]),
    ('Designated Hitter', 'brute_m', [(TOTC, 'f3DNPC_Baseball_Cult03', '00EC60')]),
    ('Doc Casey', 'thin_m', [(TOTC, 'f3DNPC_Children_Feldman', '010E39')]),
    ('Doc Winters', 'elder_m', [(TOTC, 'f3DNPC_MemoryDoc', '00F34A')]),
    ('Dutch', 'raider_m', [(TOTC, 'f3DNPC_RaiderDutch', '0038A4')]),
    ('Earthstone', 'hippie_m', [(TOTC, 'f3DNPC_Hippies_Earthstone', '002B5F')]),
    ('Father Malcom', 'thin_m', [(TOTC, 'f3DNPC_Children_Malcolm', '010681')]),
    ('Fats Dixon', 'heavy_m', [(TOTC, 'f3DNPC_Busker_Musician', '00A33E')]),
    ('Frank Taylor', 'settler_m', [(TOTC, 'f3DNPC_Mailman', '001F90')]),
    ('Franz', 'settler_m', [(TOTC, 'f3DNPC_Bunker_Franz', '006922')]),
    ('Garrett', 'settler_m', [(TOTC, 'f3DNPC_EastBoston_Garrett', '0030BB')]),
    ('Gary', 'hippie_m', [(TOTC, 'f3DNPC_Hippies_Gary', '002B36')]),
    ('Gene McConnell', 'heavy_m', [(TOTC, 'f3DNPC_Jewel_Gene', '02A84F')]),
    ('Ghost', 'ghoul_m', [(TOTC, 'f3DNPC_TremontGhost', '01C887')]),
    ('Gomez', 'settler_m', [(TOTC, 'f3DNPC_Paradise_Gomez', '006B32')]),
    ('Guthrie', 'elder_m', [(TOTC, 'f3DNPC_Treasure_Collector', '002597')]),
    ('Harold', 'elder_m', [(TOTC, 'f3DNPC_FaceDoctorHusband', '01CE05')]),
    ('Harvey (Paradise)', 'settler_m', [(TOTC, 'f3DNPC_Paradise_Harvey', '0052E9'), (TOTC, 'f3DNPC_Paradise_HarveyCorpse', '015AB7')]),
    ('Hitch', 'raider_m', [(TOTC, 'f3DNPC_AudreyTzen_Hitch', '018980')]),
    ('Infielder', 'athlete_m', [(TOTC, 'f3DNPC_Baseball_Cult06', '00EC64')]),
    ('Jeff', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerB01', '0052B3')]),
    ('Jimmy', 'settler_m', [(TOTC, 'f3DNPC_Karaoke_Jimmy', '009735')]),
    ('John (Paradise)', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerA02', '00A099')]),
    ('Johnny Friendly', 'settler_m', [(TOTC, 'f3DNPC_DiamondCity_JohnnyFriendly', '01C874')]),
    ('Joker', 'soldier_m', [(TOTC, 'f3DNPC_Gunners_Joker', '0362C7')]),
    ('Josh', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerB03', '00575D')]),
    ('Ludwick', 'soldier_m', [(TOTC, 'f3DNPC_RadstagLudwick', '00EEA6')]),
    ('Melvin', 'thin_m', [(TOTC, 'f3DNPC_EastBoston_Melvin', '004BAA')]),
    ('Murphy', 'raider_m', [(TOTC, 'f3DNPC_AudreyTzen_Murphy', '018981')]),
    ('Nilesh Singh', 'ghoul_m', [(TOTC, 'f3DNPC_Paradise_Nilesh', '015AE6')]),
    ('Outfielder', 'athlete_m', [(TOTC, 'f3DNPC_Baseball_Cult04', '00EC63')]),
    ('Overseer', 'heavy_m', [(TOTC, 'f3DNPC_R404_Overseer', '027A1D')]),
    ('Pence', 'ghoul_m', [(TOTC, 'f3DNPC_BankGhoul', '014CFA')]),
    ('Pinch Hitter', 'athlete_m', [(TOTC, 'f3DNPC_Baseball_Cult05', '00EC65')]),
    ('Powerman', 'brute_m', [(TOTC, 'f3DNPC_Powerman', '0153EA')]),
    ('Rick', 'settler_m', [(TOTC, 'f3DNPC_Paradise_SettlerA03', '00A09A')]),
    ('Russell', 'soldier_m', [(TOTC, 'f3DNPC_Jacks_BountyHunter', '034A5F')]),
    ('Sanctuary Joe', 'settler_m', [(TOTC, 'f3DNPC_Sanctuary_Joe', '0392E5')]),
    ('Scalpel', 'thin_m', [(TOTC, 'f3DNPC_Scalpel', '00484A')]),
    ('Skinny', 'heavy_m', [(TOTC, 'f3DNPC_Patriot_Coach', '0120A9')]),
    ('Slim', 'ghoul_m', [(TOTC, 'f3DNPC_JetFueled_Slim', '00560F')]),
    ('Strike', 'athlete_m', [(TOTC, 'f3DNPC_Bowling_Strike', '009923')]),
    ('Teddy', 'heavy_m', [(TOTC, 'f3DNPC_Bowling_Teddy', '009925')]),
    ('Tzen', 'officer_m', [(TOTC, 'f3DNPC_AudreyTzen_Tzen', '01421F')]),
    ('Vince', 'pretty_m', [(TOTC, 'f3DNPC_Prostitute03M', '002693')]),
    ('Vincent', 'settler_m', [(TOTC, 'f3DNPC_VincentNPC', '018205')]),
    ('Virus', 'thin_m', [(TOTC, 'f3DNPC_JetFueled_Virus', '005642')]),
    ('Von Trapp', 'soldier_m', [(TOTC, 'f3DNPC_RadstagVonTrapp', '00EEA5')]),
    ('Wolf', 'brute_m', [(TOTC, 'f3DNPC_Bunker_Wolf', '00891C')]),
    ('Colonel Whitehill', 'leader_f', [(AR2, 'AR_EnclaveColonelWhitehill', '015D95')]),
    ('Paladin Davenport', 'soldier_f', [(AR2, 'AR_BoSPaladinDavenport', '031608')]),
    ('Vanessa', 'soldier_f', [(AR2, 'AR_FuryCompanyVanessaSynth', '0278C6')]),
    ('General Ward', 'officer_m', [(AR2, 'AR_EnclaveGeneralWard', '00150A')]),
    ('Senator Matthews', 'heavy_m', [(AR2, 'AR_EnclaveSenatorMatthews', '01EFC1')]),
    ('Captain Peterson', 'officer_m', [(AR2, 'AR_EnclaveCaptainPeterson', '00150C')]),
    ('Captain Wyatt', 'officer_m', [(AR2, 'AR_EnclaveVendorCaptainWyatt', '031C40')]),
    ('Lieutenant Fitzpatrick', 'soldier_m', [(AR2, 'AR_EnclaveLieutenantFitzpatrick', '001311')]),
    ('Staff Sergeant Toby', 'soldier_m', [(AR2, 'AR_EnclaveStaffSergeantToby', '001310')]),
    ('Chief Engineer Myers', 'settler_m', [(AR2, 'AR_EnclaveChiefEngineerMyers', '01EFBD')]),
    ('Doctor Dagless', 'thin_m', [(AR2, 'AR_EnclaveVendorDoctorDagless', '00130E')]),
    ('X7-33', 'soldier_m', [(AR2, 'AR_InstituteX7-33', '028935')]),
    ('Drew', 'soldier_m', [(AR2, 'AR_MinutemenDrew', '0307BC')]),
    ('Axel', 'settler_m', [(AR2, 'AR_RETravelKMK03_Merchant', '0322AF')]),
]
