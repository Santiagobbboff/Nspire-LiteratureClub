"""Compile Sunday's two festival preparation paths and Sayori's final choice.
All dialogue and branching come from the original ch4 source AST.
"""
from rpyc_ast import load_rpyc, flatten, kind, pycode_source
from compile_day4 import Day4Compiler

class Day5Compiler(Day4Compiler):
    def linear_node(self, node, script):
        # The original Sunday script twice says `scene bedroom` instead of
        # the declared `bg bedroom`. Resolve that source spelling explicitly.
        if kind(node) == "Scene" and tuple(node.imspec[0]) == ("bedroom",):
            import copy
            node = copy.copy(node)
            node.imspec = (("bg", "bedroom"), *node.imspec[1:])
        super().linear_node(node, script)

    def binding(self, name):
        if name == "ch4_scene":
            return (6,), lambda values: {1:"natsuki",2:"yuri"}[values[6]]
        if name == "sayori_confess":
            return (7,), lambda values: bool(values[7])
        return super().binding(name)

    def python(self, node):
        source=(pycode_source(node.code) or "").strip()
        if source in ("sayori_confess = True", "sayori_confess = False"):
            self.set(7, int(source.endswith("True")))
            self.domains[7]=(int(source.endswith("True")),)
        else: super().python(node)

    def menu_prompt(self, node):
        captions=[c for c,cond,b in node.items if b]
        from source_text import SourceText
        source = SourceText(self.raw_dir)
        if captions != [source.menu("script-ch4.rpyc", 1287, i) for i in range(2)]:
            raise ValueError(f"unexpected Sunday menu: {captions}")
        return "How should I answer Sayori?"

    def dynamic_call(self, node, depth):
        if node.label != '\"ch4_exclusive_\" + ch4_scene':
            return super().dynamic_call(node, depth)
        done=self.fresh("sunday_visit_done")
        before=self.snapshot();after=[]
        for value,girl in ((1,"natsuki"),(2,"yuri")):
            body=self.fresh("sunday_visit");onward=self.fresh("sunday_next")
            self.events.append({"op":"if_state","state":6,"value":value,"target":body})
            self.jump(onward);self.label(body);self.restore(before)
            self.invoke("ch4_exclusive_"+girl,depth)
            after.append(self.snapshot());self.jump(done);self.label(onward)
        # Unsupported route values never fall into an unrelated girl's branch.
        self.jump(done);self.label(done);self.merge(after)

def extend_day5(previous):
    compiler=Day5Compiler.__new__(Day5Compiler)
    compiler.__dict__.update(previous.__dict__)
    raw_dir=compiler.raw_dir
    _,top=load_rpyc(raw_dir/"script-ch4.rpyc")
    for node in flatten(top):
        if kind(node)=="Label":compiler.labels[node.name]=("script-ch4.rpyc",node)
    compiler.domains[6]=(1,2)
    compiler.label("day5_start")
    compiler.invoke("ch4_main")
    for names in compiler.character_layers.values():
        for name in sorted(names,reverse=True):compiler.events.append({"op":"hide","resource":name})
    compiler.label("day5_end")
    from compile_day6 import extend_day6
    extend_day6(compiler)
