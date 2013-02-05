#!/usr/bin/env python
# Convert an RELAX NG compact syntax schema to a Node tree
# This file released to the Public Domain by David Mertz
#
# Extended under revised BSD license by Jan Pokorny (jpokorny@redhat.com)
# Copyright 2013 Red Hat, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the Red Hat, Inc. nor the names of its
#   contributors may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

# Differences when compared to trang output
#  1. comments placement
#  2. sometimes superfluous <group>
#  3. context-free dichotomy (diff conv08.rng.{expected,trang})
#  plenty of others (it's not the primary goal to achieve 1:1 trang match)

# XXX: each AST node has its own subclass, knows how to XMLize itself, ...?


import sys
from rnc_tokenize import tokens, pair_rules, keywords, token_list

# ONE    ... default cardinality of one
# DIRECT ... denotes that the usage of NAME is <name>, not <ref name=...>
quant_tokens_aux = tuple('''
  DIRECT
  ONE
  '''.split())

# AST nodes not directly matching the tokens
parse_constructs = tuple('''
  ROOT
  '''.split()) + tuple(r[2] for r in pair_rules)

for t in tokens + quant_tokens_aux + parse_constructs:
    globals()[t] = t

keyword_list = keywords.values()

PAIRS = {r[0]: tuple(r[1:]) for r in pair_rules}

TAGS = {
    ONE:   'group',
    SOME:  'oneOrMore',
    MAYBE: 'optional',
    ANY:   'zeroOrMore',
    ELEM:  'element',
    ATTR:  'attribute',
    NAME:  'ref',
}

URI_DATATYPES = "http://www.w3.org/2001/XMLSchema-datatypes"
URI_ANNOTATIONS = "http://relaxng.org/ns/compatibility/annotations/1.0"

DEFAULT_NAMESPACE = None
DATATYPE_LIB = [0, '"' + URI_DATATYPES + '"']
OTHER_NAMESPACE = {}
CONTEXT_FREE = 0

# debugging
for i, n in enumerate("""
    D_NOTHING
    D_TO_NODES
    D_MATCH_PAIR
    D_TYPE_BODIES
    D_NEST_DEFINES
    D_SCAN_NS
""".split()):
    globals()[n] = i and 2 << (i - 1) or 0
dlist = []
#dlist.append(D_TO_NODES)
#dlist.append(D_MATCH_PAIR)
#dlist.append(D_TYPE_BODIES)
#dlist.append(D_NEST_DEFINES)
#dlist.append(D_SCAN_NS)
debug = reduce(lambda a, b: a | b, dlist, D_NOTHING)


def try_debug(what, nodes):
    if debug & globals().get('D_' + what, D_NOTHING):
        print what
        for node in nodes:
            print node.prettyprint()


nodetypes = lambda nl: tuple(map(lambda n: n.type, nl))
toNodes = lambda toks: map(lambda t: Node(t.type, t.value), toks)


class ParseError(SyntaxError):
    pass


class Node(object):
    __slots__ = ('type', 'value', 'name', 'quant')

    def __iter__(self):
        yield self
    __len__ = lambda self: 1

    def __init__(self, type='', value=None, name=None, quant=ONE):
        self.type = type
        self.value = value if value is not None else []
        self.name = name
        self.quant = quant

    def format(self, indent=0):
        out = ['  ' * indent + repr(self)]
        write = out.append
        if isinstance(self.value, str):
            if self.type == COMMENT:
                write('  ' * (1 + indent) + self.value)
        else:
            for node in self.value:
                write(node.format(indent + 1))
        return '\n'.join(out)

    def prettyprint(self):
        print self.format()

    def toxml(self):
        if CONTEXT_FREE:
            out = []
            write = out.append
            write('<?xml version="1.0" encoding="UTF-8"?>')
            write('<grammar>')
            self.type = None
            write(self.xmlnode(1))
            write('</grammar>')
            return self.add_ns('\n'.join(out))
        else:
            return self.add_ns(self.xmlnode())

    def collect_annot(self, x):
        ret = {}
        if isinstance(x.value, basestring):
            return ret

        name, value = None, None
        for node in x.value:
            if node.type != NS_ANNOTATION:
                break
            for i, inner in enumerate(node.value):
                if i % 3 == 0 and inner.type == NAME:
                    name = inner.value
                elif i % 3 == 1 and inner.type == DEFINE:
                    name += ':' + inner.value
                elif i % 3 == 2 and inner.type == LITERAL:
                    value = inner.value
                    if ret.setdefault(name, value) is not value:
                        assert 0, "redefinition of %s" % name
                    name, value = None, None
                elif i % 3 == 0 and i > 0:
                    break
                else:
                    assert 0, "NS_ANNOTATION body does not match"
        return [n + '="' + v + '"' for n, v in ret.iteritems()]

    def xmlnode(self, indent=0):
        out = []
        write = out.append
        if self.type == ROOT:
            write('<?xml version="1.0" encoding="UTF-8"?>')

        for i, x in enumerate(self.value):
            if not isinstance(x, Node):
                raise TypeError("Unhappy Node.value: " + repr(x))
            if x.type == START:
                write('  ' * indent + '<start>')
                if (x.name is not None):
                    write('  ' * (indent + 1) + '<ref name="%s"/>' % x.name)
                else:
                    write(x.xmlnode(indent + 1))
                write('  ' * indent + '</start>')
            elif x.type == DEFINE:
                write('  ' * indent + '<define name="%s">' % x.name)
                write(x.xmlnode(indent + 1))
                write('  ' * indent + '</define>')
            elif x.type == COMMENT:
                comments = x.value.split('\n')
                if len(comments) == 1:
                    c = ' ' + comments[0] + ' '
                else:
                    c = ('\n' + '  ' * (indent + 1)).join([''] + comments + [''])
                write('  ' * indent + '<!--%s-->' % c)
            elif x.type == LITERAL:
                write('  ' * indent + '<value>%s</value>' % x.value)
            elif x.type == ANNOTATION:
                write('  ' * indent
                      + '<a:documentation>%s</a:documentation>' % x.value)
            elif x.type == INTERLEAVE:
                write('  ' * indent + '<interleave>')
                write(x.xmlnode(indent + 1))
                write('  ' * indent + '</interleave>')
            elif x.type == CHOICE:
                write('  ' * indent + '<choice>')
                write(x.xmlnode(indent + 1))
                write('  ' * indent + '</choice>')
            elif x.type in (GROUP, SEQ):
                write('  ' * indent + '<group>')
                write(x.xmlnode(indent + 1))
                write('  ' * indent + '</group>')
            elif x.type == TEXT:
                write('  ' * indent + '<text/>')
            elif x.type == EMPTY:
                write('  ' * indent + '<empty/>')
            elif x.type == DATATAG:
                DATATYPE_LIB[0] = 1     # Use datatypes
                if x.name is None:      # no paramaters
                    write('  ' * indent + '<data type="%s"/>' % x.value)
                else:
                    write('  ' * indent + '<data type="%s">' % x.name)
                    p = '<param name="pattern">%s</param>' % x.value
                    write('  ' * (indent + 1) + p)
                    write('  ' * indent + '</data>')
            elif x.type == INCLUDE:
                write('  ' * indent + '<include href="%s"/>' % x.value)
            elif x.type == NAME and x.quant == DIRECT:
                    assert x.type == NAME
                    write('  ' * indent + '<name>%s</name>' % x.value)
            elif x.type in (ATTR, ELEM, NAME):
                a = ('\n' + '  ' * (indent + 3)).join(self.collect_annot(x))
                name_n_annot = '%s' % (' ' + a).rstrip()
                name = x.value if x.type == NAME else x.name
                if name:
                    name_n_annot = ' name="%s"' % name + name_n_annot

                indent_inner = indent
                if x.quant != ONE:
                    write('  ' * indent_inner + '<%s>' % TAGS[x.quant])
                    indent_inner += 1
                tag, rest = TAGS[x.type], name_n_annot
                if x.type == NAME or x.type == ATTR and x.value[0].type == TEXT:
                    write('  ' * indent_inner + '<%s%s/>' % (tag, rest))
                else:
                    write('  ' * indent_inner + '<%s%s>' % (tag, rest))
                    write(x.xmlnode(indent_inner + 1))
                    write('  ' * indent_inner + '</%s>' % tag)
                if x.quant != ONE:
                    indent_inner -= 1
                    write('  ' * indent_inner + '</%s>' % TAGS[x.quant])

        return '\n'.join(out)

    def __repr__(self):
        return "Node(%s,%s,%s)[%d]" % (self.type, self.name,
                                       self.quant, len(self.value))

    def add_ns(self, xml):
        "Add namespace attributes to top level element"
        lines = xml.split('\n')
        self.nest_annotations(lines)  # annots not allowed before root elem
        for i, line in enumerate(lines):
            ltpos = line.find('<')
            if ltpos >= 0 and line[ltpos + 1] not in ('!', '?'):
                # We've got an element tag, not PI or comment
                tail = '>'
                new = line[:line.find(tail)]
                if new.endswith('/'):
                    new = new[:-1]
                    tail = '/' + tail
                new += ' xmlns="http://relaxng.org/ns/structure/1.0"'
                if DEFAULT_NAMESPACE is not None:
                    new += '\n    ns=%s' % DEFAULT_NAMESPACE
                if DATATYPE_LIB[0]:
                    new += '\n    datatypeLibrary=%s' % DATATYPE_LIB[1]
                for ns, url in OTHER_NAMESPACE.items():
                    new += '\n    xmlns:%s=%s' % (ns, url)
                new += tail
                lines[i] = new
                break
        return '\n'.join(lines)

    def nest_annotations(self, lines):
        "Nest any top annotation within first element"
        top_annotations = []
        for i, line in enumerate(lines[:]):
            if line.find('<a:') >= 0:
                top_annotations.append(line)
                del lines[i]
            else:
                ltpos = line.find('<')
                if ltpos >= 0 and line[ltpos + 1] not in ('!', '?'):
                    break
        for line in top_annotations:
            lines.insert(i, '  ' + line)


def findmatch(beg, nodes, offset):
    level = 1
    end = PAIRS[beg][0]
    for i, t in enumerate(nodes[offset:]):
        if t.type == beg:
            level += 1
        elif t.type == end:
            level -= 1
        if level == 0:
            return i + offset
    raise EOFError("No closing token encountered for %s @ %d"
                   % (beg, offset))


#
# 1st pass in the pipe
#

def match_pairs(nodes):
    """<left paren., []> + <tokens> + <right paren., []>  -->  <ent., <tokens>>

    Other effects:
        - merge comments/annotations
    """
    newnodes = []
    i = 0
    while 1:
        if i >= len(nodes):
            break
        node = nodes[i]
        if node.type in PAIRS.keys():
            # TOKEN, etc. -> NAME where suitable
            # (keyword-like names do not need to be escaped in some cases)
            if node.type == 'BEG_BODY' and newnodes[-1].type in keyword_list:
                if newnodes[-2].type in (ELEM, ATTR):
                    newnodes[-1].type = NAME
            # Look for enclosing brackets
            match = findmatch(node.type, nodes, i + 1)
            matchtype = PAIRS[node.type][1]
            node = Node(type=matchtype, value=nodes[i + 1:match])
            node.value = match_pairs(node.value)
            newnodes.append(node)
            i = match + 1
        elif (node.type in (COMMENT, ANNOTATION) and i > 0
          and newnodes[-1].type == node.type):
            # merge comments/annotations
            newnodes[-1].value += "\n" + node.value
            i += 1
        else:
            newnodes.append(node)
            i += 1
        if i >= len(nodes):
            break
        if nodes[i].type in (ANY, SOME, MAYBE):
            newnodes[-1].quant = nodes[i].type
            i += 1

    nodes[:] = newnodes
    return nodes


#
# 2nd pass in the pipe
#

def type_bodies(nodes):
    """Another (main) de-linearization"""
    newnodes = []
    i = 0
    while 1:
        if i >= len(nodes):
            break
        if (nodetypes(nodes[i:i + 3]) == (ELEM, NAME, BODY)
          or nodetypes(nodes[i:i + 3]) == (ATTR, NAME, BODY)):
            name, body = nodes[i + 1].value, nodes[i + 2]
            value, quant = type_bodies(body.value), body.quant
            node = Node(nodes[i].type, value, name, quant)
            newnodes.append(node)
            if not name:
                assert False
            i += 3
        # "element a|b" cases
        elif (nodetypes(nodes[i:i + 3]) == (ELEM, NAME, CHOICE)
          or nodetypes(nodes[i:i + 3]) == (ATTR, NAME, CHOICE)):
            # see nameClass (choice of nameClass+)
            # XXX: very simplified
            if nodes[i].type == ATTR:
                assert False
            node_type = nodes[i].type
            value = [nodes[i + 1]]
            i += 2
            while nodetypes(nodes[i:i + 2]) == (CHOICE, NAME):
                value.extend(type_bodies(nodes[i:i + 2]))
                i += 2
            # re-mark quant as we do not want "ref" output here
            for v in value:
                if v.type == NAME:
                    v.quant = DIRECT
            assert len(nodes) >= i and nodes[i].type == BODY
            value.extend(type_bodies(nodes[i].value))
            node = Node(node_type, value, None, nodes[i].quant)
            i += 1
            newnodes.append(node)
        elif nodetypes(nodes[i:i + 2]) == (DATATAG, PATTERN):
            node = Node(DATATAG, nodes[i + 1].value, nodes[i].value)
            newnodes.append(node)
            i += 2
        else:
            n = nodes[i]
            if n.type == GROUP:   # Recurse into groups
                value = type_bodies(n.value)
                if len(value) > 1 and n.type:
                    n = Node(GROUP, value, None, n.quant)
            newnodes.append(n)
            i += 1
    nodes[:] = newnodes
    return nodes


#
# 3rd pass in the pipe
#

def _nest_annotations(nodes, mapping, delim=None):
    """Helper to move comments/annotations down into attributes/elements

    Uses non-tail recursion to proceed the tree bottom-up as
    otherwise there would be confusion if the annotations are
    newly added (and thus should be kept) or the original ones
    to be moved.

    Mapping is partially defined
        token-type |-> accumulator-list for token-type
    for token-types covering annotations (ANNOTATION, NS_ANNOTATION)
    and is used to pass unconsumed annotations down the tree.

    Returns triplet: number of consumed nodes, filtered nodes, mapping.

    Note that mapping should contain empty lists only when the recursion
    returns back to the initiator (XXX: little bit of sanity checking,
    we cannot speak about proper validation here).
    """
    # XXX: unclean, yes
    newnodes = []
    for i, n in enumerate(nodes):
        if delim and n.type == delim:
            break

        if not isinstance(n.value, str):  # no recurse to terminal str
            if n.type in (ELEM, ATTR):
                mapping_rec = {n: [] for n in
                               (ANNOTATION, NS_ANNOTATION, COMMENT)}
            else:
                mapping_rec = mapping
            _nest_annotations(n.value, mapping_rec)

            if n.type in (ELEM, ATTR):  # annot. consumer (guarded in recursion)
                n.value = (mapping['NS_ANNOTATION'] + mapping['ANNOTATION']
                           + mapping['COMMENT'] + n.value)
                mapping['NS_ANNOTATION'][:], mapping['ANNOTATION'][:] = [], []
                mapping['COMMENT'][:] = []
        elif i == len(nodes) - 1 and n.type == COMMENT and not delim:
            # comment at the end of the nodelist, but only if not top-level
            newnodes.append(n)
            continue

        mapping.get(n.type, newnodes).append(n)

    nodes[:] = newnodes
    return i, nodes, mapping


def _intersperse(nodes):
    """Look for interleaved, choice, or sequential nodes in groups/bodies"""
    for node in nodes:
        if node.type in (ELEM, ATTR, GROUP, LITERAL):  # XXX: literal?
            val = node.value
            ntypes = [n.type for n in val if not isinstance(val, str)]
            inters = [t for t in ntypes if t in (INTERLEAVE, CHOICE, SEQ)]
            inters = dict(zip(inters, [0] * len(inters)))
            if len(inters) > 1:
                raise ParseError("Ambiguity in sequencing: %s" % node)
            if len(inters) > 0:
                intertype = inters.keys()[0]
                outer_items, last_ntype, internode = [], None, None
                simplify = node.type == GROUP
                for pat in node.value:
                    if pat.type == intertype:
                        if internode is None:
                            internode = Node(intertype, [outer_items.pop()])
                            outer_items.append(internode)
                        # otherwise drop it
                    elif last_ntype == intertype:
                        internode.value.append(pat)
                    else:
                        outer_items.append(pat)
                        if pat.type in (COMMENT, ANNOTATION):
                            # these are not interesting wrt. last type
                            continue
                        elif pat.quant not in (ONE, MAYBE):
                            simplify = False
                    last_ntype = pat.type

                if (simplify and len(outer_items) == 1
                  and outer_items[0] is internode):
                    node.type, node.value = internode.type, internode.value
                else:
                    node.value = outer_items
        if not isinstance(node.value, str):  # No recurse to terminal str
            _intersperse(node.value)
    return nodes


def nest_defines(nodes):
    """Attach groups to named patterns

    Other effects:
        - annotations are properly nested
        - comments are nested
    """
    newnodes = []
    i = 0
    group, annotations, ns_annotations, comments = [], [], [], []
    mapping = dict(ANNOTATION=annotations, NS_ANNOTATION=ns_annotations,
                   COMMENT=comments)
    while i < len(nodes):
        node = nodes[i]
        newnodes.append(node)
        group[:], annotations[:], ns_annotations[:], comments[:] = [], [], [], []
        if node.type == DEFINE:
            j, group[:], mapping = _nest_annotations(nodes[i + 1:], mapping, DEFINE)
            i += j
            node.name = node.value
            grp = _intersperse([Node(GROUP, group[:])])[0]
            if len(grp.value) > 1 and grp.type != SEQ:
                node.value = [grp]
            else:
                node.value = grp.value[:]
            # when _nest_annotations returned *not* due to reaching DEFINE,
            # but trailing comments are tolerated
            if i + 1 > len(nodes) or nodes[i + 1].type not in (DEFINE, COMMENT):
                break
        elif node.type == ELEM:
            # top-level element
            _intersperse(Node(GROUP, [node]))
        i += 1
    nodes[:] = newnodes
    return nodes


#
# 4th pass in the pipe
#

def scan_NS(nodes):
    """Look for any namespace configuration lines

    Other effects:
        - DEFINE(start) --> START
    """
    global DEFAULT_NAMESPACE, OTHER_NAMESPACE, CONTEXT_FREE
    for node in nodes:
        if node.type == DEFAULT_NS:
            DEFAULT_NAMESPACE = node.value
        elif node.type == NS:
            ns, url = map(str.strip, node.value.split('=', 1))
            OTHER_NAMESPACE[ns] = url
        elif node.type == ANNOTATION and 'a' not in OTHER_NAMESPACE:
            OTHER_NAMESPACE['a'] = '"' + URI_ANNOTATIONS + '"'
        elif node.type == DATATYPES:
            DATATYPE_LIB[:] = [1, node.value]
        elif not CONTEXT_FREE and node.type == DEFINE and node.name == 'start':
            CONTEXT_FREE = 1
            node.type = START
            node.name = None


def make_nodetree(tokens):
    """Wraps the pipe of conversion passes"""
    nodes = toNodes(tokens)
    try_debug('TO_NODES', nodes)

    match_pairs(nodes)
    try_debug('MATCH_PAIR', nodes)

    type_bodies(nodes)
    try_debug('TYPE_BODIES', nodes)

    nest_defines(nodes)
    try_debug('NEST_DEFINES', nodes)

    scan_NS(nodes)
    try_debug('SCAN_NS', nodes)

    return Node(ROOT, nodes)


if __name__ == '__main__':
    print make_nodetree(token_list(sys.stdin.read())).toxml()
