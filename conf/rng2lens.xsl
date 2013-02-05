<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:rng="http://relaxng.org/ns/structure/1.0"
    xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">

<!--
  Copyright 2013 Red Hat, Inc.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  - Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  - Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  - Neither the name of the Red Hat, Inc. nor the names of its
    contributors may be used to endorse or promote products derived from this
    software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
-->

<!--
  TODO: migrate to using Build Augeas "library"
  -->

<!-- prologue -->
<xsl:import href="common.xsl"/>
<xsl:import href="xsddatatypes2lens.xsl"/>
<xsl:output method="text"/>
<xsl:strip-space elements="*"/>

<!-- xsltdoc (http://www.kanzaki.com/parts/xsltdoc.xsl) header -->
<xsl:template name="_doas_description">
    <rdf:RDF xmlns="http://purl.org/net/ns/doas#">
        <rdf:Description rdf:about="">
            <title>rng2lens.xsl XSLT stylesheet</title>
            <description>
                This stylesheet generates Augeas lens from RELAX NG schema
                modeling corosync configuration (as reified in the form
                of corosync.conf).

                INPUT:  corosync.rnc converted 1:1 to non-compact syntax
                        (no simplification)
                OUTPUT: Augeas lens for corosync.conf
            </description>
            <author rdf:parseType="Resource">
                <name>Jan Pokorný</name>
                <mbox rdf:resource="jpokorny@redhat.com"/>
            </author>
            <created>2013-02-01</created>
            <release rdf:parseType="Resource">
                <revision>0.2</revision>
                <created>2013-02-04</created>
            </release>
            <rights>Copyright 2013 Red Hat, Inc.</rights>
            <license rdf:resource="http://opensource.org/licenses/BSD-3-Clause"/>
            <acknowledgement>
                Based on previous work of Angus Salkeld et al.
            </acknowledgement>
        </rdf:Description>
    </rdf:RDF>
</xsl:template>

<!--
     helpers
 -->

<xsl:variable name="SP"><xsl:text> </xsl:text></xsl:variable>
<xsl:variable name="NL"><xsl:text>&#xA;</xsl:text></xsl:variable>
<xsl:variable name="NLNL"><xsl:text>&#xA;&#xA;</xsl:text></xsl:variable>
<xsl:variable name="QUOT"><xsl:text>"</xsl:text></xsl:variable>

<xsl:template name="string-replace-all">
    <!--** Replaces each occurrence of 'replace' in 'string' with 'by'. -->
    <xsl:param name="string"/>
    <xsl:param name="replace"/>
    <xsl:param name="by"/>
    <xsl:choose>
        <xsl:when test="contains($string, $replace)">
            <xsl:value-of select="substring-before($string, $replace)"/>
            <xsl:value-of select="$by"/>
            <!-- recursion -->
            <xsl:call-template name="string-replace-all">
                <xsl:with-param name="string"
                                select="substring-after($string, $replace)"/>
                <xsl:with-param name="replace" select="$replace"/>
                <xsl:with-param name="by" select="$by"/>
            </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
            <xsl:value-of select="$string"/>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

<xsl:template name="serialize-per-name-plus">
    <!--** Generates "A|B|C" with extra decorations via modes. -->
    <xsl:param name="items"/>
    <xsl:for-each select="$items">
        <xsl:variable name="prefix">
            <xsl:choose>
                <xsl:when test="name() = 'attribute'">
                    <xsl:apply-templates select="." mode="plus-prefix-option"/>
                </xsl:when>
            </xsl:choose>
        </xsl:variable>
        <xsl:variable name="suffix">
            <xsl:choose>
                <xsl:when test="name() = 'attribute'">
                    <xsl:apply-templates select="." mode="plus-suffix-option"/>
                </xsl:when>
            </xsl:choose>
        </xsl:variable>
        <!--@Serialization itself, using obtained prefix/suffix.-->
        <xsl:choose>
            <xsl:when test="position() = 1">
                <xsl:value-of select="concat($NL, '    ', $prefix, @name, $suffix)"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="concat($NL, '    |', $prefix, @name, $suffix)"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
</xsl:template>

<!--
    configuration
 -->

<xsl:variable name="toplevel-name">lns</xsl:variable>

<!-- XXX: this can be overriden from autotools -->
<xsl:param name="config-file"
           select="concat($QUOT, '/etc/corosync/corosync.conf', $QUOT)"/>

<xsl:variable name="head">(* generated, no point in editing this *)
(* Process /etc/corosync/corosync.conf                             *)
(* The lens is based on the corosync RELAX NG schema defining      *)
(* corosync configuration (same base for corosync.conf man page)   *)
module Corosync =

autoload xfm

let comment = Util.comment
let empty = Util.empty
let dels = Util.del_str
let eol = Util.eol

let ws = del /[ \t]+/ " "
let wsc = del /:[ \t]+/ ": "
let indent = del /[ \t]*/ ""
(* We require that braces are always followed by a newline *)
let obr = del /\{([ \t]*)\n/ "{\n"
let cbr = del /[ \t]*}[ \t]*\n/ "}\n"

let ikey (k:regexp) = indent . key k

let section (n:regexp) (b:lens) =
  [ ikey n . ws . obr . (b|empty|comment)* . cbr ]

let kv (k:regexp) (v:regexp) =
  [ ikey k .  wsc . store v . eol ]

(* FIXME: it would be much more concise to write                       *)
(* [ key k . ws . (bare | quoted) ]                                    *)
(* but the typechecker trips over that                                 *)
let qstr (k:regexp) =
  let delq = del /['"]/ "\"" in
  let bare = del /["']?/ "" . store /[^"' \t\n]+/ . del /["']?/ "" in
  let quoted = delq . store /.*[ \t].*/ . delq in
  [ ikey k . wsc . bare . eol ]
 |[ ikey k . wsc . quoted . eol ]

</xsl:variable>

<!--
     declarative attitude
 -->

<xsl:variable name="start-name" select="/rng:grammar/rng:start/rng:ref/@name"/>
<xsl:variable name="start"
              select="/rng:grammar/rng:define[@name = $start-name]"/>

<xsl:key name="definekey" match="/rng:grammar/rng:define" use="@name"/>
<xsl:key name="refkey" match="rng:ref" use="@name"/>

<xsl:template match="/">
    <!--** Triggers template, wrapping processed top element with head/tail. -->
    <xsl:value-of select="$head"/>
    <xsl:apply-templates select="$start"/>
    <xsl:value-of select="concat('let xfm = transform ', $toplevel-name)"/>
    <xsl:value-of select="concat(' (incl ', QUOT, $config-file, QUOT, ')')"/>
</xsl:template>

<xsl:template match="rng:define | rng:element">
    <!--** Sections handling. -->
    <xsl:choose>
        <xsl:when test="name() = 'define' and rng:element[@name = current()/@name]">
            <!--@Delegate to nested element with the same @name if suitable.-->
            <xsl:apply-templates select="rng:element[@name = current()/@name]"/>
        </xsl:when>
        <xsl:otherwise>
            <xsl:variable name="secs-required"
                          select="(current()
                                  |current()/rng:group
                                  |current()/rng:interleave
                                  |current()/rng:group/rng:interleave
                                  )/rng:ref"/>
            <xsl:variable name="secs-optional"
                          select="(current()/rng:zeroOrMore
                                  |current()/rng:group/rng:zeroOrMore
                                  |current()/rng:interleave/rng:optional
                                  |current()/rng:group/rng:interleave/rng:optional
                                  |current()/rng:optional
                                  |current()/rng:group/rng:optional
                                  )/rng:ref"/>
            <xsl:variable name="opts-required"
                          select="(.
                                  |rng:group
                                  )/rng:attribute"/>
            <xsl:variable name="opts-optional"
                          select="(.
                                  |rng:group
                                  )/rng:optional/rng:attribute"/>

            <xsl:if test="name() = 'element' and $start//rng:ref[@name = current()/@name]">
                <xsl:value-of select="concat('(* The ', @name,
                                             ' section *)', $NL)"/>
            </xsl:if>

            <!--@Emit non-duplicated definitions we depend on (-> postorder).-->
            <xsl:for-each select="$secs-required|$secs-optional">
                <xsl:if test="generate-id() = generate-id(key('refkey', @name)[1])">
                    <xsl:apply-templates select="key('definekey', @name)"/>
                </xsl:if>
            </xsl:for-each>

            <!--@Determine initial part.-->
            <xsl:choose>
                <xsl:when test="@name = $start-name">
                    <xsl:value-of select="concat('let ', $toplevel-name,
                                                 ' = (comment|empty|')"/>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:value-of select="concat('let ', @name, ' =')"/>
                    <xsl:if test="name() = 'element'">
                        <xsl:value-of select="concat($NL, '  let setting =')"/>
                    </xsl:if>
                </xsl:otherwise>
            </xsl:choose>

            <!--@Determine body.-->
            <xsl:call-template name="serialize-per-name-plus">
                <xsl:with-param name="items"
                                select="$secs-required
                                       |$secs-optional
                                       |$opts-required
                                       |$opts-optional"/>
            </xsl:call-template>

            <!--@Determine final part.-->
            <xsl:choose>
                <xsl:when test="@name = $start-name">
                    <xsl:text>)*</xsl:text>
                </xsl:when>
                <xsl:when test="name() = 'element'">
                    <xsl:value-of select="concat(' in', $NL, '  section ',
                                                 $QUOT, @name, $QUOT,
                                                 ' setting')"/>
                </xsl:when>
            </xsl:choose>
            <xsl:value-of select="$NLNL"/>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

<xsl:template match="*" mode="plus-prefix-option">
    <!--** Option has extra line prefix: 'kv' or 'qstr'. -->
    <xsl:choose>
        <xsl:when test="rng:choice or rng:data[@type != '']">
            <!--@Start kv (finalized in 'plus-suffix-option' mode).-->
            <xsl:value-of select="concat('kv', $SP, $QUOT)"/>
        </xsl:when>
        <xsl:otherwise>
            <!--@Start qstr (finalized in 'plus-suffix-option' mode).-->
            <xsl:value-of select="concat('qstr', $SP, '/')"/>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

<xsl:template match="*" mode="plus-suffix-option">
    <!--** Option has extra line suffix: (type/enumerated values). -->
    <xsl:choose>
        <xsl:when test="rng:choice">
            <!--@Finalize kv with enumerated values forming target regexp.-->
            <xsl:value-of select="concat($QUOT, $SP)"/>
            <xsl:text>/</xsl:text>
                <xsl:call-template name="text-join">
                    <xsl:with-param name="items" select="rng:choice/rng:value"/>
                    <xsl:with-param name="sep" select="'|'"/>
                </xsl:call-template>
            <xsl:text>/</xsl:text>
        </xsl:when>
        <xsl:when test="rng:data[@type != '']">
            <!--@Finalize kv with explicit type (see xsddatatypes2lens.xsl).-->
            <xsl:value-of select="concat($QUOT, $SP)"/>
            <xsl:call-template name="xsddatatype2lens">
                <xsl:with-param name="type" select="rng:data/@type"/>
            </xsl:call-template>
        </xsl:when>
        <xsl:otherwise>
            <!--@Finalize qstr.-->
            <xsl:value-of select="'/'"/>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

</xsl:stylesheet>
<!-- vim: set et ts=4 sw=4: -->
