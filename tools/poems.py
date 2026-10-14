"""Read Act 1 poems as data, and retain their original author fonts."""
import ast
from pathlib import Path
from PIL import Image
from rpyc_ast import flatten, kind, load_rpyc, pycode_source

ACT1_POEMS = {'poem_s1', 'poem_s2', 'poem_s3', 'poem_n1', 'poem_n2', 'poem_n3',
              'poem_n3b', 'poem_y1', 'poem_y2', 'poem_y3', 'poem_y3b',
              'poem_m1', 'poem_m2', 'poem_m3',
              # Act 2
              'poem_y22', 'poem_y23', 'poem_n2b', 'poem_n23', 'poem_m21', 'poem_m22'}
FONT_INFO = {'sayori': ('s1.ttf', 0xE200, 18, 25),
             'natsuki': ('n1.ttf', 0xE400, 15, 20),
             'yuri': ('y1.ttf', 0xE600, 20, 28),
             'monika': ('m1.TTF', 0xE800, 18, 25),
             # Yuri's Act 2 handwriting degrades: y2 for "Wheel", y3 for the final poem.
             'yuri2': ('y2.ttf', 0xEC00, 18, 26),
             'yuri3': ('y3.ttf', 0xEE00, 16, 24)}
PAPERS = {'poem_y1': 'images/bg/poem_y1.jpg', 'poem_y2': 'images/bg/poem_y2.jpg'}


def load_poems(raw_dir):
    _, top = load_rpyc(raw_dir / 'poems.rpyc')
    poems = {}
    for node in flatten(top):
        if kind(node) != 'Python':
            continue
        tree = ast.parse(pycode_source(node.code) or '')
        for assignment in tree.body:
            if not isinstance(assignment, ast.Assign):
                continue
            name = getattr(assignment.targets[0], 'id', '')
            if name not in ACT1_POEMS:
                continue
            call = assignment.value
            if not isinstance(call, ast.Call) or getattr(call.func, 'id', '') != 'Poem':
                raise ValueError(f'Unexpected poem definition: {name}')
            poems[name] = {kw.arg: ast.literal_eval(kw.value) for kw in call.keywords}
            # Author glyph banks cover U+0000-U+01FF; fold typographic punctuation.
            for key in ('title', 'text'):
                if isinstance(poems[name].get(key), str):
                    poems[name][key] = (poems[name][key].replace('\u2019', "'").replace('\u2018', "'")
                                        .replace('\u201c', '"').replace('\u201d', '"')
                                        .replace('\u2014', '-').replace('\u2026', '...'))
            if poems[name].get('yuri_2'):
                poems[name]['author'] = 'yuri2'
            elif poems[name].get('yuri_3'):
                poems[name]['author'] = 'yuri3'
    if poems.keys() != ACT1_POEMS:
        raise ValueError('Missing Act 1 poem definitions')
    return poems


def prepare_poems(manifest, raw_dir, art_dir, font_dir):
    poems = load_poems(raw_dir)
    # Preserve the original sheet proportions, centered on the 320x240 screen.
    Image.open(raw_dir / 'images/bg/poem.jpg').convert('RGB').resize(
        (267, 240), Image.Resampling.LANCZOS).save(art_dir / 'img/poem_paper.png')
    manifest['resources'].append({'name': '__poem_paper', 'path': 'img/poem_paper.png',
                                  'kind': 'poem_paper'})
    for paper, path in PAPERS.items():
        Image.open(raw_dir / path).convert('RGB').resize(
            (267, 240), Image.Resampling.LANCZOS).save(art_dir / f'img/{paper}.png')
        manifest['resources'].append({'name': f'__poem_paper_{paper}', 'path': f'img/{paper}.png',
                                      'kind': 'poem_paper'})
    manifest['poem_fonts'] = []
    for author, (filename, base, size, leading) in FONT_INFO.items():
        used = [p['title'] + '\n\n' + p['text'] for p in poems.values() if p['author'] == author]
        manifest['poem_fonts'].append({'path': str((font_dir / filename).resolve()),
                                       'base': base, 'size': size, 'texts': used})
    for event in manifest['events']:
        if event['op'] != 'poem':
            continue
        poem = poems[event['poem']]
        _, _, size, leading = FONT_INFO[poem['author']]
        paper = f"__poem_paper_{event['paper']}" if event.get('paper') in PAPERS else '__poem_paper'
        event.update(resource=paper, text_en=poem['title'] + '\n\n' + poem['text'],
                     author=poem['author'], font_size=size, line_height=leading)
