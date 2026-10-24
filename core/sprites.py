"""Masques de sprites du MAIN OS 1.13 du Model:Cycles : de la place libérable (notes/14 §5).

Les grands blocs 0xFF de l'image ne sont pas des caves libres : ce sont les MASQUES de
sprites (classe Bitmap, constructeur 0x40070172). Chaque sprite a deux plans de même
taille : l'image (champ +0x10) et le masque (champ +0x14). Quatre masques sont
entièrement à 0xFF (opaques), donc identiques octet pour octet.

On garde intact le plus grand (celui du sprite 32x260, 1040 o) et on fait pointer les
autres dessus, en réécrivant la constante 32 bits passée au constructeur. Le rendu est
identique (mêmes octets lus) et le plan d'origine du masque devient réellement libre.
Le destructeur ne libère pas ces plans (sprite statique, drapeau +0x18 = 0).

Un tweak qui veut une de ces zones inclut redirect_write(zone) : build.py voit alors que
la seule référence vers la zone est réécrite et accepte d'y écrire.

Même principe pour vingt-deux sprites 47x47 (constantes 0x400ac2b2..0x400b133e) : leurs masques
de 376 o sont identiques (un carré opaque de 47 colonnes, octets ff ff ff ff ff fe 00 00 par
ligne). On garde celui de 0x40172220 ; les autres peuvent pointer dessus (notes/32 §11). L'arpégiateur
en utilise deux, le clavier d'accords quatorze (notes/42 §6), cinq restent libres.
Chacun n'est désigné que par la constante du constructeur. Ces masques ne sont pas à 0xFF :
le contrôle des caves de build.py ne les voit pas, les octets « old » des écritures si.

Le relevé complet (notes/52) compte 10 groupes de masques identiques, 171 masques, 161 libérables
(GROUPS ci-dessous) : les 22 de 47x47 et, par exemple, 43 de 48x22 (192 o) ou 20 de 35x35 (280 o).
On garde la première copie de chaque groupe. MASKS les reprend tous, avec les trois masques 0xFF
du début ; tools/registry.py dit qui utilise lesquels (tweaks/model-cycles_OS1.13/REGISTRY.md) et
vérifie le relevé sur l'OS officiel (--cycles).
"""
BASE = 0x40000400
SHARED_MASK = 0x40154ae4        # masque du sprite 32x260 (1040 o), gardé intact
SHARED_47 = 0x40172220          # masque d'un sprite 47x47 (376 o), gardé intact

# va du masque : (taille, va de la constante 32 bits qui le désigne, description[, masque partagé])
MASKS = {
    0x4015c044: (720, 0x400b6434, "sprite 64x90 (cadre), constructeur 0x400b6432"),
    0x4016cae8: (1024, 0x400b1106, "sprite 64x128 (damier de test), constructeur 0x400b1104"),
    0x4018a788: (1024, 0x400ad1aa, "sprite 64x128 (damier inverse), constructeur 0x400ad1a8"),
}

# Groupes de masques identiques : (largeur, hauteur) -> (taille, va du masque gardé, [(va du masque, va de la
# constante qui le désigne), ...]). Relevé sur le MAIN OS officiel (notes/52) ; vérifié par tools/registry.py --cycles.
GROUPS = {
    (47, 47): (376, 0x40172220, [
        (0x4016b6f8, 0x400b133e), (0x4016b9e8, 0x400b131c), (0x40171f30, 0x400b05a0), (0x40172220, 0x400b0580),
        (0x40172608, 0x400b0544), (0x40179730, 0x400af64a), (0x40182b38, 0x400adfda), (0x40182e28, 0x400adfba),
        (0x40183118, 0x400adf9e), (0x40185018, 0x400adba8), (0x40185968, 0x400ada00), (0x40185c58, 0x400ad9e0),
        (0x40189930, 0x400ad328), (0x4018a220, 0x400ad202), (0x4018cd48, 0x400acdb2), (0x4018d1b8, 0x400acd76),
        (0x4018d4a8, 0x400acd56), (0x4018dba8, 0x400accfe), (0x4018f4b4, 0x400ac8d0), (0x4018fc74, 0x400ac81c),
        (0x401904b4, 0x400ac784), (0x40192734, 0x400ac2b2),
    ]),
    (48, 22): (192, 0x4014b364, [
        (0x4014b364, 0x400baf6a), (0x40152d38, 0x400b9726), (0x401592bc, 0x400b7404), (0x4015943c, 0x400b73e4),
        (0x4015f38c, 0x400b504a), (0x40165fec, 0x400b28b4), (0x40168a84, 0x400b1bc0), (0x40168da4, 0x400b1b32),
        (0x4016bcd8, 0x400b12fe), (0x4016dab8, 0x400b0f78), (0x4016fae8, 0x400b0b20), (0x40170c48, 0x400b08da),
        (0x40171a48, 0x400b06ec), (0x40172a68, 0x400b0508), (0x40172be8, 0x400b04ec), (0x401736c8, 0x400b036c),
        (0x40177f28, 0x400af962), (0x40178708, 0x400af812), (0x40178fc0, 0x400af6e2), (0x401792b0, 0x400af6a6),
        (0x40179430, 0x400af686), (0x401795b0, 0x400af66a), (0x40179b18, 0x400af60e), (0x4017c97c, 0x400aee4e),
        (0x401829b8, 0x400adff6), (0x40183408, 0x400adf7e), (0x40184808, 0x400adc40), (0x40184af8, 0x400adc04),
        (0x40184c78, 0x400adbe4), (0x40187528, 0x400ad742), (0x401876a8, 0x400ad726), (0x40188f98, 0x400ad3a0),
        (0x4018a608, 0x400ad1c6), (0x4018b9c8, 0x400ad048), (0x4018bb48, 0x400ad028), (0x4018cbc8, 0x400acdce),
        (0x4018d038, 0x400acd92), (0x4018d798, 0x400acd3a), (0x4018f80c, 0x400ac894), (0x40190184, 0x400ac7e0),
        (0x40190334, 0x400ac7a4), (0x4019089c, 0x400ac748), (0x40192a24, 0x400ac292),
    ]),
    (35, 35): (280, 0x4014a660, [
        (0x4014a660, 0x400bb21c), (0x4014ab5c, 0x400bb120), (0x4014b85c, 0x400bae4e), (0x4014d74c, 0x400bac28),
        (0x401542f8, 0x400b91d8), (0x401548b4, 0x400b903a), (0x4015b8f8, 0x400b668c), (0x4015f50c, 0x400b5028),
        (0x401601fc, 0x400b4b4a), (0x40160864, 0x400b48ac), (0x40160b6c, 0x400b4870), (0x40160e6c, 0x400b482e),
        (0x401625bc, 0x400b3e2a), (0x40163fb8, 0x400b34c8), (0x4016616c, 0x400b2898), (0x40166760, 0x400b272e),
        (0x40167c50, 0x400b211e), (0x401696a0, 0x400b1540), (0x401699a8, 0x400b1500), (0x4016aa28, 0x400b1480),
    ]),
    (34, 34): (272, 0x4016bfc8, [
        (0x4016bfc8, 0x400b12b8), (0x4016f8c8, 0x400b0b3c), (0x40173848, 0x400b0350), (0x401780a8, 0x400af946),
        (0x401784e8, 0x400af832), (0x401835b8, 0x400adedc), (0x40184df8, 0x400adbc8), (0x40185308, 0x400adb8c),
        (0x40185f48, 0x400ad9c4), (0x40186238, 0x400ad988), (0x40189618, 0x400ad364), (0x4018af88, 0x400ad18a),
        (0x4018b1a8, 0x400ad16e), (0x4018ff64, 0x400ac7fc), (0x40192ba4, 0x400ac276),
    ]),
    (27, 27): (108, 0x4014a550, [
        (0x4014a550, 0x400bb258), (0x4014a890, 0x400bb1fc), (0x4014aa34, 0x400bb178), (0x4014af84, 0x400bb044),
        (0x4014b05c, 0x400bb028), (0x4014b5b4, 0x400baf2e), (0x4014b784, 0x400bae6e), (0x4014e608, 0x400ba8f2),
        (0x40152f88, 0x400b96ce), (0x40159a04, 0x400b71ea), (0x40159adc, 0x400b71ce), (0x4015b820, 0x400b66a8),
        (0x4015bf24, 0x400b6476), (0x4015c9a4, 0x400b6232), (0x4015fb6c, 0x400b4e5a), (0x40160a94, 0x400b488c),
        (0x401617a8, 0x400b4386), (0x40161880, 0x400b4366), (0x401629dc, 0x400b3d96), (0x40163a60, 0x400b362e),
        (0x40167900, 0x400b21ac), (0x40167aa8, 0x400b216c), (0x40168440, 0x400b1d7c), (0x401688b4, 0x400b1c5e),
        (0x401698d0, 0x400b1522),
    ]),
    (31, 22): (124, 0x4016e980, [
        (0x4016e980, 0x400b0d6c), (0x4016f7d0, 0x400b0b5c), (0x4016fd60, 0x400b0ae4), (0x40172510, 0x400b0564),
        (0x40178888, 0x400af7f6), (0x40179a20, 0x400af62e), (0x4017a4d4, 0x400af43c), (0x401837d8, 0x400adeba),
        (0x401871a0, 0x400ad77e), (0x40189838, 0x400ad344), (0x4018a510, 0x400ad1e6), (0x4018bcc8, 0x400ad00c),
        (0x4018f98c, 0x400ac874), (0x4018fa84, 0x400ac858), (0x4018fb7c, 0x400ac838), (0x401907a4, 0x400ac768),
        (0x40190a1c, 0x400ac72c), (0x40194ad4, 0x400abeaa), (0x40195a7c, 0x400abc76),
    ]),
    (26, 26): (104, 0x4014b4e4, [
        (0x4014b4e4, 0x400baf4a), (0x401573cc, 0x400b83da), (0x40160d9c, 0x400b4850), (0x40162ab4, 0x400b3d76),
        (0x401630e4, 0x400b3b2e), (0x401638c0, 0x400b366a), (0x40163990, 0x400b364a), (0x40163b38, 0x400b360e),
        (0x40166690, 0x400b274c), (0x401679d8, 0x400b218a), (0x40167b80, 0x400b2144), (0x40168c4c, 0x400b1b80),
        (0x40168fb4, 0x400b1a72), (0x40179c98, 0x400af5f2), (0x40186168, 0x400ad9a4),
    ]),
    (46, 31): (184, 0x4016be58, [
        (0x4016be58, 0x400b12dc), (0x40170ad8, 0x400b08f6), (0x40171bc8, 0x400b06d0), (0x401728f8, 0x400b0528),
        (0x40179140, 0x400af6c2), (0x40184988, 0x400adc20), (0x40185648, 0x400ada58), (0x401857f8, 0x400ada1c),
    ]),
    (41, 41): (328, 0x40187298, [
        (0x40187298, 0x400ad762), (0x4018d918, 0x400acd1a),
    ]),
    (52, 22): (208, 0x4014bfe4, [
        (0x4014bfe4, 0x400bad54), (0x4016ea78, 0x400b0d50),
    ]),
}

# La constante suit l'opcode de 2 octets (pea abs.l 0x4879, ou move.l #imm,(sp) 0x2ebc) : le constructeur commence
# 2 octets avant. Le masque gardé de chaque groupe n'entre pas dans MASKS : on n'y écrit jamais.
for (_w, _h), (_size, _kept, _rows) in GROUPS.items():
    for _va, _const in _rows:
        if _va != _kept:
            MASKS[_va] = (_size, _const, f"sprite {_w}x{_h}, constructeur {_const - 2:#x}", _kept)
assert SHARED_47 == GROUPS[(47, 47)][1]


def kept_of(mask_va):
    """VA du masque gardé sur lequel on redirige mask_va (le 32x260 pour les masques 0xFF)."""
    return (MASKS[mask_va][3:] or (SHARED_MASK,))[0]


def redirect_write(mask_va):
    """Écriture JSON qui fait pointer le sprite de mask_va sur le masque partagé."""
    ptr, shared = MASKS[mask_va][1], kept_of(mask_va)
    return {"off": ptr - BASE, "old": mask_va.to_bytes(4, "big").hex(),
            "new": shared.to_bytes(4, "big").hex()}


def zone(mask_va):
    """(va, taille) de la zone libérée."""
    return mask_va, MASKS[mask_va][0]
