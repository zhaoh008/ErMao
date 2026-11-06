#!/usr/bin/env python3
"""Bake the curated Q-version ink-wash asset pack into engine/assets/inkwash.

Usage: python3 tools/import_inkwash_assets.py [asset-pack-dir]
Source: user-provided asset pack, default D:\GameEngine\game\Q版水墨国风（行侠仗义五千年）
Only the subset used by the 2D survivors demo is copied; sprites are normalized
into horizontal sheets so the existing SpriteAnimatorComponent can play them.
"""
import os
import math
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageEnhance

import sys
SRC = sys.argv[1] if len(sys.argv) > 1 else r'D:\GameEngine\game\Q版水墨国风（行侠仗义五千年）'
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
OUT = os.path.join(ROOT, 'engine', 'assets', 'inkwash')
os.makedirs(OUT, exist_ok=True)

PAPER = None

def load(folder, name):
    return Image.open(os.path.join(SRC, folder, name)).convert('RGBA')

def trim(im):
    box = im.getchannel('A').getbbox()
    return im.crop(box) if box else im

def save(im, name, max_size=None):
    im = im.convert('RGBA')
    if max_size:
        w, h = im.size
        s = min(1.0, max_size / max(w, h))
        if s < 1.0:
            im = im.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    path = os.path.join(OUT, name)
    im.save(path, optimize=True)
    print('  ', name, im.size, os.path.getsize(path))
    return im

def paper_base(w, h, darkness=1.0):
    base = load('场景背景', 'sand.png').resize((w, h), Image.LANCZOS)
    # warm rice paper tint
    tint = Image.new('RGBA', (w, h), (238, 228, 204, 255))
    base = Image.blend(base.convert('RGBA'), tint, 0.45)
    if darkness != 1.0:
        base = ImageEnhance.Brightness(base).enhance(darkness)
    return base

# ---------------------------------------------------------------- menu key art
def build_menu():
    W, H = 1600, 900
    base = paper_base(W, H, 1.0)
    # ink landscape on the right: full valley art, faded into the paper at its left edge
    land = load('场景背景', 'jj_bg_03.png')
    lh = H + 24
    lw = round(land.width * lh / land.height)
    land = land.resize((lw, lh), Image.LANCZOS)
    mask = Image.new('L', land.size, 255)
    md = ImageDraw.Draw(mask)
    fade = round(lw * 0.45)
    for x in range(fade):
        md.line([(x, 0), (x, lh)], fill=int(255 * (x / fade) ** 0.8))
    land.putalpha(mask)
    base.alpha_composite(land, (W - lw + 120, -12))
    # black ink brush stroke (use its alpha so no grey rectangle is pasted in)
    brush = trim(load('场景背景', 'beijing_001.png'))
    bw = 760
    bh = round(brush.height * bw / brush.width)
    brush = brush.resize((bw, bh), Image.LANCZOS)
    ink = Image.new('RGBA', brush.size, (26, 25, 22, 0))
    ink.putalpha(brush.getchannel('A').point(lambda a: int(a * 0.55)))
    base.alpha_composite(ink, (36, H - bh + 20))
    # red seal
    seal = Image.new('RGBA', (76, 76), (0, 0, 0, 0))
    sd = ImageDraw.Draw(seal)
    sd.rounded_rectangle([2, 2, 73, 73], radius=6, fill=(150, 48, 38, 220))
    try:
        f = ImageFont.truetype('/mnt/c/Windows/Fonts/msyh.ttc', 36)
        sd.text((14, 16), '墨', font=f, fill=(244, 228, 198, 240))
    except Exception:
        pass
    base.alpha_composite(seal, (W - 128, 34))
    base.convert('RGB').save(os.path.join(OUT, 'menu.jpg'), quality=88, optimize=True)

# ---------------------------------------------------------------- 9-slice widgets
def build_widgets():
    # panel: dark lacquered paper with a gold/ink double border
    S = 256
    panel = paper_base(S, S, 0.20)
    d = ImageDraw.Draw(panel)
    d.rounded_rectangle([5, 5, S - 6, S - 6], radius=10, outline=(176, 132, 74, 235), width=3)
    d.rounded_rectangle([11, 11, S - 12, S - 12], radius=8, outline=(74, 54, 36, 255), width=2)
    d.rounded_rectangle([16, 16, S - 17, S - 17], radius=6, outline=(206, 164, 96, 120), width=1)
    save(panel, 'panel.png')

    # skill slot: pack's gold selection frame over a dark ink tile
    slot = paper_base(128, 128, 0.24)
    sd = ImageDraw.Draw(slot)
    sd.rounded_rectangle([4, 4, 123, 123], radius=8, fill=(30, 24, 20, 245))
    frame = load('UI界面', 'common_frame_xuanzhongkuang.png').resize((128, 128), Image.LANCZOS)
    slot = Image.alpha_composite(slot, frame)
    save(slot, 'slot.png')

    # buttons: pack art, kept as-is (light jade / amber / cinnabar)
    for out, folder, name in [
        ('btn_normal.png', 'UI界面', 'common_btn_median_green.png'),
        ('btn_hover.png', 'UI界面', 'common_btn_median_yellow.png'),
        ('btn_pressed.png', 'UI界面', 'common_btn_median_red.png'),
        ('btn_primary.png', 'UI界面', 'rc_btn.png'),
    ]:
        im = load(folder, name)
        save(im, out)
    # disabled: desaturated dark jade
    dis = load('UI界面', 'common_btn_median_green.png')
    dis = ImageEnhance.Color(dis).enhance(0.25)
    dis = ImageEnhance.Brightness(dis).enhance(0.55)
    dis.putalpha(dis.getchannel('A').point(lambda a: int(a * 0.85)))
    save(dis, 'btn_disabled.png')

    # bars: dark track + pack gold fill, tinted for health / xp
    track = paper_base(128, 18, 0.22)
    td = ImageDraw.Draw(track)
    td.rounded_rectangle([1, 1, 126, 17], radius=6, fill=(24, 19, 16, 245), outline=(168, 126, 72, 220), width=1)
    save(track, 'bar_track.png')
    gold = trim(load('UI界面', 'log_progressbar_loading_bar.png')).resize((128, 12), Image.LANCZOS)

    def tint_bar(base, rgb, name):
        px = base.convert('RGBA')
        r, g, b, a = px.split()
        color = Image.new('RGBA', px.size, rgb + (255,))
        mixed = Image.blend(px, color, 0.55)
        mixed.putalpha(a)
        save(mixed, name)
    tint_bar(gold, (226, 74, 66), 'bar_health.png')
    tint_bar(gold, (92, 208, 190), 'bar_xp.png')

    # crest: ink circle + sword
    crest = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    circle = load('UI界面', 'common_board_top_shuimo_01.png').resize((128, 128), Image.LANCZOS)
    crest = Image.alpha_composite(crest, circle)
    sword = trim(load('道具图标', 'icon_jian_10002.png'))
    sword.thumbnail((104, 104), Image.LANCZOS)
    crest.alpha_composite(sword, ((128 - sword.width) // 2, (128 - sword.height) // 2))
    save(crest, 'crest.png')

    # chest icons
    save(trim(load('UI界面', 'sc_icon_bx01.png')), 'chest.png', max_size=256)
    save(trim(load('UI界面', 'sc_icon_bx01_open.png')), 'chest_open.png', max_size=256)
    # pickups
    save(trim(load('道具图标', 'icon_mlj01.png')), 'gem.png', max_size=48)
    save(trim(load('UI界面', 'hulu.png')), 'health.png', max_size=64)
    save(trim(load('道具图标', 'icon_sh01.png')), 'xp.png', max_size=64)

    # skill icons
    icons = ['icon_gong_20002.png', 'icon_skill_23.png', 'icon_skill_37.png', 'icon_skill_28.png',
             'icon_skill_25.png', 'icon_sg_fwjt.png', 'icon_skill_09.png', 'icon_skill_22.png',
             'icon_skill_03.png', 'icon_skill_35.png']
    for i, name in enumerate(icons):
        save(trim(load('道具图标', name)), f'icon_{i}.png', max_size=128)

    # heroes for class cards / in-game player（职业立绘：游侠 / 剑士 / 法师）
    # 只用「角色立绘」文件夹的静态立绘；玩家角色不做行走动画，只按朝向翻转。
    # 2D 朝向约定：所有角色/怪物贴图统一烘焙成面朝右（右 = +X），运行期只用 FaceMovementX。
    # 逐张标记源图是否需要镜像：郭蛇雕原画面朝右，令狐啸/莫愁姐原画面朝左。
    heroes = [
        ('mp_img_taohuagu_guoshediao.png', True),
        ('mp_img_huashanpai_linghuxiao.png', False),
        ('mp_img_shenmupai_mochoujie.png', True),
    ]
    for i, (name, mirror) in enumerate(heroes):
        im = trim(load('角色立绘', name))
        if mirror:
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
        save(im, f'hero_{i}.png', max_size=360)

    # scenery props
    props = [
        ('prop_tree.png', 'gaibang_zhangaiwu_1.png'),
        ('prop_censer.png', 'shaolin_zhangaiwu_1.png'),
        ('prop_rock.png', 'taohuagu_zhangaiwu_1.png'),
        ('prop_lantern.png', 'wudangpai_zhangaiwu_1.png'),
        ('prop_jar.png', 'wujianposui-guanzi-idle_0.png'),
        ('prop_urn.png', 'emeipai_zhangaiwu_1.png'),
    ]
    for out, name in props:
        save(trim(load('场景背景', name)), out, max_size=256)

    # ink decoration
    save(trim(load('UI界面', 'common_board_inkmask.png')), 'ink_splash.png', max_size=256)
    save(trim(load('场景背景', 'beijing_001.png')), 'ink_stroke.png', max_size=512)

    # ground tile: seamless grass from the map pack
    ground = load('场景背景', 'taohuagu_dibiao.png').convert('RGB').resize((1024, 1024), Image.LANCZOS)
    # desaturate a little toward rice-paper ink green
    gray = ImageEnhance.Color(ground).enhance(0.78)
    tint = Image.new('RGB', gray.size, (226, 222, 198))
    ground = Image.blend(gray, tint, 0.10)
    ground.save(os.path.join(OUT, 'ground.png'), optimize=True)
    print('   ground.png', ground.size, os.path.getsize(os.path.join(OUT, 'ground.png')))

# ---------------------------------------------------------------- characters
PLAYER = {
    0: 'gongshou1',
    1: 'cike',
    2: 'yiren',
}
ENEMY = {
    0: 'qiangdao1',
    1: 'shitouren',
    2: 'ying',
    3: 'zhizhu',
    4: 'guanbing',
    5: 'laohu',
}

def frames(group, action):
    root = os.path.join(SRC, '动画序列')
    items = []
    for name in os.listdir(root):
        if not name.startswith(group + '-' + action + '_'):
            continue
        stem = name[len(group + '-' + action + '_'):]
        num = ''.join(ch for ch in stem if ch.isdigit())
        items.append((int(num or 0), name))
    items.sort()
    return [load('动画序列', n) for _, n in items]

def pack_sheet(images, out_name, mirror=False):
    if mirror:
        images = [im.transpose(Image.FLIP_LEFT_RIGHT) for im in images]
    boxes = [im.getchannel('A').getbbox() for im in images]
    boxes = [b for b in boxes if b]
    u0 = min(b[0] for b in boxes); v0 = min(b[1] for b in boxes)
    u1 = max(b[2] for b in boxes); v1 = max(b[3] for b in boxes)
    cw, ch = u1 - u0, v1 - v0
    pad = 4
    S = max(cw, ch) + pad * 2
    S = (S + 3) // 4 * 4
    sheet = Image.new('RGBA', (S * len(images), S), (0, 0, 0, 0))
    for i, im in enumerate(images):
        crop = im.crop((u0, v0, u1, v1))
        x = i * S + (S - cw) // 2
        y = S - pad - ch
        sheet.alpha_composite(crop, (x, y))
    save(sheet, out_name)
    return S, len(images)

def build_characters():
    for i, group in PLAYER.items():
        for action in ['idle', 'run']:
            pack_sheet(frames(group, action), f'player_{i}_{action}.png')
    for i, group in ENEMY.items():
        for action in ['idle', 'run', 'death']:
            fr = frames(group, action)
            if not fr:
                continue
            # 怪物序列帧原画统一面朝左/正面，烘焙时统一镜像成面朝右。
            pack_sheet(fr, f'enemy_{i}_{action}.png', mirror=True)

# ---------------------------------------------------------------- effects
EFFECTS = [
    ('arrow.png', 'arrow_02_Tex.png', 0),
    ('fx_blade.png', 'xulie_dao_004_2x2_Tex.png', 0),
    ('fx_sword.png', 'wcf_jian_01_Tex.png', 0),
    ('fx_lightning.png', 'xulie_shandian072_1x4_Tex.png', 0),
    ('fx_lightning_ring.png', 'ring_shandian_0013_Tex.png', 0),
    ('fx_frost.png', 'xuehua_008_2x2_Tex.png', 0),
    ('fx_frost_ring.png', 'ring_shuimo_0832_Tex.png', 0),
    ('fx_poison.png', 'yanwu_027_4x4_Tex.png', 0),
    ('fx_poison_ground.png', 'du_lvdi_001_Tex.png', 0),
    ('fx_fireball.png', 'fire_026_4x4_Tex.png', 0),
    ('fx_explosion.png', 'fire_034_zj_2_4x4_Tex.png', 0),
    ('fx_meteor.png', 'shitou_008_2x2_Tex.png', 0),
    ('fx_singularity.png', 'blackhole_001_2_Tex.png', 0),
    ('fx_singularity_ring.png', 'ring_615_Tex.png', 0),
    ('fx_prism.png', 'long_003_2_cl_Tex.png', 0),
    ('fx_plague.png', 'bao_003_2x2_Tex.png', 0),
    ('fx_ink.png', 'shuimo_002_Tex.png', 0),
    ('fx_slash.png', 'xulie_ci_003_2x4_Tex.png', 0),
]

def build_effects():
    for out, name, _ in EFFECTS:
        im = load('特效序列', name)
        # keep sprite sheets small for the mini-game package
        maxsize = 256 if max(im.size) <= 512 else 512
        save(im, out, max_size=maxsize)

if __name__ == '__main__':
    print('[menu]'); build_menu()
    print('[widgets]'); build_widgets()
    print('[characters]'); build_characters()
    print('[effects]'); build_effects()
    print('done ->', OUT)
