#!/usr/bin/env python
# Define the tokenizer for RELAX NG compact syntax
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

# TODO: update positions so they can be used for debugging

import re
try:
    from ply import lex
except ImportError:
    import lex


#
# tokens declaration
#

# square (left + right delimiter) together with single-node tree replacement
pair_rules = tuple(l.replace(' ', '').split(':') for l in '''
  BEG_BODY  : END_BODY  : BODY
  BEG_PAREN : END_PAREN : GROUP
  BEG_ANNO  : END_ANNO  : NS_ANNOTATION
  '''.strip().splitlines())
pair_tokens = reduce(lambda a, b: a + tuple(b[:2]), pair_rules, ())

quant_tokens = tuple('''
  ANY
  MAYBE
  SOME
  '''.split())

immediate_tokens = tuple('''
  ANNOTATION
  CHOICE
  COMMENT
  DATATAG
  DEFINE
  EQUAL
  INTERLEAVE
  LITERAL
  NAME
  PATTERN
  SEQ
  WHITESPACE
  '''.split()) + quant_tokens

# http://relaxng.org/compact-20021121.html#syntax
keywords = {
    'attribute':  'ATTR',
    'default':    'DEFAULT_NS',
    'datatypes':  'DATATYPES',
    'div':        'DIV',
    'element':    'ELEM',
    'empty':      'EMPTY',
    'external':   'EXTERNAL',
    'grammar':    'GRAMMAR',
    'include':    'INCLUDE',
    'inherit':    'INHERIT',
    'list':       'LIST',
    'mixed':      'MIXED',
    'namespace':  'NS',
    'notAllowed': 'NOTALLOWED',
    'parent':     'PARENT',
    'start':      'START',
    'string':     'STRING',
    'text':       'TEXT',
    'token':      'TOKEN',
}

tokens = immediate_tokens + pair_tokens + tuple(keywords.values())


#
# tokens definition
#

# RELAX NG/XML datatype NCName is defined in
#   http://books.xmlschemata.org/relaxng/ch19-77215.html
#   http://www.w3.org/TR/REC-xml-names/#NT-NCName
# which can be resolved as
#   <NCName> ::= <Start> <NonStart>*
# where
#   <Start> ::= [A-Z] | "_" | [a-z] | [#xC0-#xD6] | ...
#     (see http://www.w3.org/TR/REC-xml/#NT-NameStartChar)
#   <NonStart> ::= <Start> | "-" | "." | [0-9] | #xB7 | ..
#     (see http://www.w3.org/TR/REC-xml/#NT-NameChar)
#
# NOTE: skipping [\u10000-\uEFFFF] as it won't get lex'd (???)
NCName_start = "(?:[A-Z_a-z]" \
    u"|[\u00C0-\u00D6]" \
    u"|[\u00D8-\u00F6]" \
    u"|[\u00F8-\u02FF]" \
    u"|[\u0370-\u037D]" \
    u"|[\u037F-\u1FFF]" \
    u"|[\u200C-\u200D]" \
    u"|[\u2070-\u218F]" \
    u"|[\u2C00-\u2FEF]" \
    u"|[\u3001-\uD7FF]" \
    u"|[\uF900-\uFDCF]" \
    u"|[\uFDF0-\uFFFD]" \
    ")"
NCName_nonstart = NCName_start + "|(?:[-.0-9]" \
    u"|\u00B7"          \
    u"|[\u0300-\u036F]" \
    u"|[\u203F-\u2040]" \
    ")"
NCName = NCName_start + "(?:" + NCName_nonstart + ")*"

# lex internals

t_ignore = " \t\n"


def t_error(t):
    try:
        t.lexer.skip(1)
    except AttributeError:
        # works in historic version of PLY
        t.skip(1)

# immediate tokens

t_ANY        = r'\*'
t_CHOICE     = r'\|'
t_EQUAL      = r'='
t_INTERLEAVE = r'&'
t_MAYBE      = r'\?'
t_SEQ        = r','
t_SOME       = r'\+'
t_WHITESPACE = r'\s+'


def t_ANNOTATION(t):
    r"\#\#[ \t]?.*"
    t.value = t.value.replace('# ', '#', 1).split('##', 1)[1].rstrip()
    return t


def t_COMMENT(t):
    r"\#[ \t]?.*"
    t.value = t.value.replace('# ', '#', 1).split('#', 1)[1].rstrip()
    return t


def t_DATATYPES(t):
    r"datatypes\s+xsd\s*=\s*.*"
    t.value = t.value.split('=', 1)[1].strip()
    return t


def t_DATATAG(t):
    r"xsd:\w+"
    t.value = t.value.split(':', 1)[1]
    return t


def t_DEFAULT_NS(t):
    r"default\s+namespace\s*=\s*.*"
    t.value = t.value.split('=', 1)[1].strip()
    return t


def t_INCLUDE(t):
    t.value = t.value.split('"', 1)[1][:-1]
    return t
t_INCLUDE.__doc__ = r'include\s*"' + NCName + '"'


def t_LITERAL(t):
    r'".+?"(?:\s*[~]\s*".+?")*'
    t.value = ' '.join(i.strip(' \n"') for i in t.value.split('~'))
    return t


def t_NAME(t):
    # "In order to use a keyword as an identifier, it must be quoted with \."
    t.value = t.value[2:]
    return t
t_NAME.__doc__ = r"\\[.]" + NCName


def t_NS(t):
    r"namespace\s+.*"
    t.value = t.value.split(None, 1)[1]
    return t


def t_PATTERN(t):
    r'{\s*pattern\s*=\s*".*"\s*}'
    t.value = t.value[:-1].split('=', 1)[1].strip()[1:-1]
    return t


# these two last (and in this order) for reason
def t_DEFINE(t):
    t.value = t.value.split('=', 1)[0].strip()
    return t
t_DEFINE.__doc__ = NCName + "\s*="


def t_ID(t):
    t.type = keywords.get(t.value, 'NAME')    # Check for keywords
    return t
t_ID.__doc__ = NCName


# pair tokens

t_BEG_ANNO  = r'\['
t_END_ANNO  = r'\]'
t_BEG_BODY  = r'{'
t_END_BODY  = r'}'
t_BEG_PAREN = r'\('
t_END_PAREN = r'\)'


#
# processing
#

def preprocess(rnc):
    # 2.2. BOM stripping
    if len(rnc) >= 2 and ord(rnc[0]) == 0xFE and ord(rnc[1]) == 0xFF:
        rnc = rnc[2:]
    # 2.3 Newline normalization
    rnc = re.sub(r"(?:\n|\r\n?)", "\n", rnc, re.MULTILINE)
    # TODO: 2.4 Escape interpretation
    return rnc


def token_list(rnc):
    lex.lex()
    lex.input(preprocess(rnc))
    ts = []
    while 1:
        t = lex.token()
        if t is None:
            break
        ts.append(t)
    return ts


if __name__ == '__main__':
    import sys
    del t_ignore
    tokens = token_list(sys.stdin.read())
    print '\n'.join(map(repr, tokens))
