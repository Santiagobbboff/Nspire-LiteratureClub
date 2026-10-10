"""Read Act 1 poems as data, and retain their original author fonts."""
import ast
from pathlib import Path
from PIL import Image
from rpyc_ast import flatten, kind, load_rpyc, pycode_source

ACT1_POEMS = {'poem_s1', 'poem_s2', 'poem_s3', 'poem_n1', 'poem_n2', 'poem_n3',
              'poem_n3b', 'poem_y1', 'poem_y2', 'poem_y3', 'poem_y3b',
              'poem_m1', 'poem_m2', 'poem_m3'}
FONT_INFO = {'sayori': ('s1.ttf', 0xE200, 18, 25),
             'natsuki': ('n1.ttf', 0xE400, 15, 20),
             'yuri': ('y1.ttf', 0xE600, 20, 28),
             'monika': ('m1.TTF', 0xE800, 18, 25)}


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
        event.update(resource='__poem_paper', text_en=poem['title'] + '\n\n' + poem['text'],
                     author=poem['author'], font_size=size, line_height=leading)
