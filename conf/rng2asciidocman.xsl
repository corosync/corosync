<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
    xmlns:rng="http://relaxng.org/ns/structure/1.0"
    xmlns:a="http://relaxng.org/ns/compatibility/annotations/1.0"
    xmlns:a4doc="http://people.redhat.com/jpokorny/ns/a4doc"
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
  TODO: cross-references
  TODO: support annotations of the sections
  TODO: dependency on simplification can be removed using similar construction
        as used in rng2lens.xsl, IOW. named set of options would be described
        and further referred (wrapped with some wording like "see the named
        set of options XY" ... wait, expanding it fully may be actually better)
 -->

<!-- prologue -->
<xsl:import href="common.xsl"/>
<xsl:output method="text"/>
<xsl:strip-space elements="*"/>

<!-- xsltdoc (http://www.kanzaki.com/parts/xsltdoc.xsl) header -->
<xsl:template name="_doas_description">
    <rdf:RDF xmlns="http://purl.org/net/ns/doas#">
        <rdf:Description rdf:about="">
            <title>rng2asciidocman.xsl XSLT stylesheet</title>
            <description>
                This stylesheet generates AsciiDoc formatted man page from
                RELAX NG schema modeling corosync configuration (as reified
                in the form of corosync.conf).

                INPUT:  corosync.rnc converted to non-compact syntax
                        + simplified (step7.20)
                OUTPUT: AsciiDoc document suitable for -> DocBook -> man page
                        proceeding
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
                Based on previous work of Steven Dake et al.
            </acknowledgement>
        </rdf:Description>
    </rdf:RDF>
</xsl:template>

<!--
     helpers
 -->

<xsl:variable name="bold" select="'*'"/>
<xsl:variable name="underline" select='"&apos;"'/>
<xsl:variable name="SP"><xsl:text> </xsl:text></xsl:variable>
<xsl:variable name="NL"><xsl:text>&#xA;</xsl:text></xsl:variable>
<xsl:variable name="NLNL"><xsl:text>&#xA;&#xA;</xsl:text></xsl:variable>
<xsl:variable name="NLNLNL"><xsl:text>&#xA;&#xA;&#xA;</xsl:text></xsl:variable>

<xsl:template name="string-replace-all">
    <!--** Replaces each occurrence of 'replace' in 'string' with 'by'. -->
    <xsl:param name="string"/>
    <xsl:param name="replace"/>
    <xsl:param name="by"/>
    <xsl:choose>
        <xsl:when test="contains($string, $replace)">
            <xsl:value-of select="substring-before($string, $replace)"/>
            <xsl:value-of select="$by"/>
            <!--@Recursion.-->
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

<xsl:template name="serialize-per-name">
    <!--** Generates "A, B and C" with optional markup of items. -->
    <xsl:param name="items"/>
    <xsl:param name="markup" value="''"/>
    <xsl:for-each select="$items">
        <xsl:choose>
            <xsl:when test="position() = 1">
                <xsl:value-of select="concat($markup, @name, $markup)"/>
            </xsl:when>
            <xsl:when test="position() = last()">
                <xsl:value-of select="concat(' and ', $markup, @name, $markup)"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="concat(', ', $markup, @name, $markup)"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
</xsl:template>

<xsl:template name="context-description">
    <!--** Generates context description. -->
    <xsl:param name="items"/>
    <xsl:choose>
        <xsl:when test='@name != $start-name'>
            <xsl:choose>
                <xsl:when test="count($items) &lt;= 1">
                    <xsl:text>subsection of </xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>subsections of </xsl:text>
                </xsl:otherwise>
            </xsl:choose>
            <xsl:value-of select="concat($bold, @name, $bold)"/>
            <xsl:text> section</xsl:text>
            <xsl:choose>
                <xsl:when test="count($items) = 0"/>
                <xsl:when test="count($items) = 1">
                    <xsl:text> is</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text> are</xsl:text>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:when>
        <xsl:otherwise>
            <xsl:choose>
                <xsl:when test="count($items) = 0">
                    <xsl:text>top-level section</xsl:text>
                </xsl:when>
                <xsl:when test="count($items) = 1">
                    <xsl:text>top-level section is</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>top-level sections are</xsl:text>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

<!--
    configuration
 -->

<xsl:variable name="head">// generated, no point in editing this
COROSYNC_CONF(5)
================
:doctype: manpage
:man source: corosync Man Page
//:man version:
:man manual: Corosync Cluster Engine Programmer's Manual
// workaround AsciiDoc not supporting glosslist as it's in DocBook5+
:listtags-glossary.list: &lt;glosslist{id? id="{id}"}{role? role="{role}"}{reftext? xreflabel="{reftext}"}&gt;{title?&lt;title&gt;{title}&lt;/title&gt;}|&lt;/glosslist&gt;


NAME
----
corosync.conf - corosync executive configuration file


SYNOPSIS
--------
`/etc/corosync/corosync.conf`


DESCRIPTION
-----------
The *`corosync.conf`* instructs the corosync executive about various parameters
needed to control the corosync executive.

The configuration file consists top level directives that mostly denote
bracketed configuration sections (i.e., wrapped by a pair of '{', '}'
characters and identified by the preceeding directive).  The content of
a section consists of new-line separated configuration items, option-value
pairs delimited by ':', interleaved with likewise formatted subsections.
Empty lines and lines starting with '#' character are ignored.


CONFIGURATION SECTIONS AND OPTIONS
----------------------------------
</xsl:variable>

<xsl:variable name="tail">

FILES
-----
[glossary]
*`/etc/corosync/corosync.conf`*::
    The corosync executive configuration file.


SEE ALSO
--------
*corosync_overview*(8), *votequorum*(5), *logrotate*(8)
</xsl:variable>

<!--
     declarative attitude
 -->

<xsl:variable name="start-name" select="/rng:grammar/rng:start/rng:ref/@name"/>
<xsl:variable name="start"
              select="/rng:grammar/rng:define[@name = $start-name]/rng:element"/>

<xsl:template match="/">
    <!--** Triggers template, wrapping processed top element with head/tail. -->
    <xsl:value-of select="$head"/>
    <xsl:apply-templates select="$start"/>
    <xsl:value-of select="$tail"/>
</xsl:template>

<xsl:template match="rng:element">
    <!--** Section handling. -->
    <xsl:variable name="secs-required"
                  select="/rng:grammar/rng:define[
                          @name =
                              (current()
                              |current()/rng:group
                              |current()/rng:interleave
                              |current()/rng:group/rng:interleave
                              )/rng:ref/@name
                          ]/rng:element[
                              a:documentation or current() != $start
                          ]"/>
    <xsl:variable name="secs-optional"
                  select="/rng:grammar/rng:define[
                          @name =
                              (current()/rng:zeroOrMore
                              |current()/rng:group/rng:zeroOrMore
                              |current()/rng:interleave/rng:optional
                              |current()/rng:group/rng:interleave/rng:optional
                              |current()/rng:optional
                              |current()/rng:group/rng:optional
                              )/rng:ref/@name
                          ]/rng:element[
                              a:documentation or current() != $start
                          ]"/>

    <xsl:if test="a:documentation">
        <xsl:call-template name="string-replace-all">
            <xsl:with-param name="string" select="a:documentation"/>
            <xsl:with-param name="replace" select="$NLNL"/>
            <xsl:with-param name="by" select="concat($NL, '+', $NL)"/>
        </xsl:call-template>
        <xsl:value-of select="$NL"/>
    </xsl:if>

    <xsl:choose>
        <xsl:when test="$secs-required or $secs-optional">
            <xsl:if test="$secs-required">
                <xsl:variable name="context-description">
                    <xsl:call-template name="context-description">
                        <xsl:with-param name="items" select="$secs-required"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:variable name="secs-required-line">
                    <xsl:call-template name="serialize-per-name">
                        <xsl:with-param name="items" select="$secs-required"/>
                        <xsl:with-param name="markup" select="$bold"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:text>Required </xsl:text>
                <xsl:value-of select="concat($context-description, $SP,
                                             $secs-required-line)"/>
                <xsl:choose>
                    <xsl:when test="$secs-optional">
                        <xsl:text>,</xsl:text>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:text>.</xsl:text>
                    </xsl:otherwise>
                </xsl:choose>
                <xsl:value-of select="$NL"/>
            </xsl:if>
            <!-- sep -->
            <xsl:if test="$secs-optional">
                <xsl:variable name="context-description">
                    <xsl:call-template name="context-description">
                        <xsl:with-param name="items" select="$secs-optional"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:variable name="secs-optional-line">
                    <xsl:call-template name="serialize-per-name">
                        <xsl:with-param name="items" select="$secs-optional"/>
                        <xsl:with-param name="markup" select="$bold"/>
                    </xsl:call-template>
                </xsl:variable>
                <xsl:choose>
                    <xsl:when test="$secs-required">
                        <xsl:text>optional </xsl:text>
                    </xsl:when>
                    <xsl:otherwise>
                        <xsl:text>Optional </xsl:text>
                    </xsl:otherwise>
                </xsl:choose>
                <xsl:value-of select="concat($context-description, $SP,
                                             $secs-optional-line, '.', $NL)"/>
            </xsl:if>
        </xsl:when>
        <xsl:otherwise>
            <xsl:variable name="context-description">
                <xsl:call-template name="context-description">
                    <xsl:with-param name="items" select="$secs-required"/>
                </xsl:call-template>
            </xsl:variable>
            <xsl:text>There is no </xsl:text>
            <xsl:value-of select="$context-description"/>
            <xsl:text>.</xsl:text>
            <xsl:value-of select="$NL"/>
        </xsl:otherwise>
    </xsl:choose>
    <xsl:value-of select="$NL"/>

    <xsl:call-template name="proceed-options">
        <xsl:with-param name="opts-required"
                        select="(.
                                |rng:group
                                )/rng:attribute[a:documentation]"/>
        <xsl:with-param name="opts-optional"
                        select="(.
                                |rng:group
                                )/rng:optional/rng:attribute[a:documentation]"/>
    </xsl:call-template>

    <xsl:call-template name="proceed-sections">
        <xsl:with-param name="secs-required"
                        select="$secs-required"/>
        <xsl:with-param name="secs-optional"
                        select="$secs-optional"/>
    </xsl:call-template>
</xsl:template>

<xsl:template match="rng:attribute">
    <!--** Option handling. -->
    <xsl:variable name="content">
        <xsl:call-template name="string-replace-all">
            <xsl:with-param name="string" select="a:documentation"/>
            <xsl:with-param name="replace" select="$NLNL"/>
            <xsl:with-param name="by" select="concat($NL, '+', $NL)"/>
        </xsl:call-template>
    </xsl:variable>
    <xsl:value-of select="$NL"/>
    <xsl:value-of select="concat($bold, @name, $bold)"/>
    <xsl:if test="@a:defaultValue">
        <xsl:value-of select="concat(' (default: ', $underline, @a:defaultValue, $underline,')')"/>
    </xsl:if>
    <xsl:text>::</xsl:text>
    <xsl:value-of select="concat($NLNL, $content)"/>
    <xsl:if test="rng:choice">
        <!--@Show enumerated values.-->
        <xsl:value-of select="$NL"/>
        <xsl:text>Possible values: </xsl:text>
            <xsl:call-template name="text-join">
                <xsl:with-param name="items" select="rng:choice/rng:value"/>
                <xsl:with-param name="sep" select="', '"/>
                <xsl:with-param name="markup" select="$underline"/>
            </xsl:call-template>
        <xsl:text>.</xsl:text>
    </xsl:if>
    <xsl:for-each select="@a4doc:*[contains(local-name(), '-hint')]">
        <!--@Extra handling of per-option a4doc annotations.-->
        <xsl:value-of select="$NLNL"/>
        <xsl:choose>
            <xsl:when test="local-name() = 'danger-hint'">
                <xsl:text>CAUTION: </xsl:text>
            </xsl:when>
            <xsl:when test="local-name() = 'discretion-hint'">
                <xsl:text>IMPORTANT: </xsl:text>
            </xsl:when>
            <xsl:when test="local-name() = 'deprecation-hint'">
                <xsl:text>WARNING: This has been deprecated. </xsl:text>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text>WARNING: </xsl:text>
            </xsl:otherwise>
        </xsl:choose>
        <xsl:value-of select="normalize-space(.)"/>
        <xsl:value-of select="$NL"/>
    </xsl:for-each>
    <xsl:value-of select="$NLNL"/>
</xsl:template>

<!--
    procedural attitude
 -->

<xsl:template name="proceed-sections">
    <!--** Required/optional sections handling. -->
    <xsl:param name="secs-required" value="false()"/>
    <xsl:param name="secs-optional" value="false()"/>

    <!-- cannot use $secs-required|$secs-optional (we need explicit order) -->
    <xsl:for-each select="$secs-required">
        <xsl:value-of select="concat($NL, '=== ', @name, $NLNLNL)"/>
        <xsl:apply-templates select="."/>
    </xsl:for-each>

    <xsl:for-each select="$secs-optional">
        <xsl:value-of select="concat($NL, '=== ', @name, $NLNLNL)"/>
        <xsl:apply-templates select="."/>
    </xsl:for-each>
</xsl:template>

<xsl:template name="proceed-options">
    <!--** Required/optional options handling. -->
    <xsl:param name="opts-required" value="false()"/>
    <xsl:param name="opts-optional" value="false()"/>

    <xsl:variable name="opts-required-line">
        <xsl:call-template name="serialize-per-name">
            <xsl:with-param name="items" select="$opts-required"/>
            <xsl:with-param name="markup" select="$underline"/>
        </xsl:call-template>
    </xsl:variable>
    <xsl:variable name="opts-optional-line">
        <xsl:call-template name="serialize-per-name">
            <xsl:with-param name="items" select="$opts-optional"/>
            <xsl:with-param name="markup" select="$underline"/>
        </xsl:call-template>
    </xsl:variable>

    <xsl:if test="$opts-required">
        <xsl:value-of select="$NL"/>
        <xsl:choose>
            <xsl:when test="count($opts-required) = 1">
                <xsl:text>==== Required option</xsl:text>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text>==== Required options</xsl:text>
            </xsl:otherwise>
        </xsl:choose>
        <xsl:value-of select="$NL"/>
        <xsl:apply-templates select="$opts-required"/>
    </xsl:if>

    <xsl:if test="$opts-optional">
        <xsl:value-of select="$NL"/>
        <xsl:choose>
            <xsl:when test="count($opts-optional) = 1">
                <xsl:text>==== Optional or conditionally required option</xsl:text>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text>==== Optional or conditionally required options</xsl:text>
            </xsl:otherwise>
        </xsl:choose>
        <xsl:value-of select="$NL"/>
        <xsl:apply-templates select="$opts-optional"/>
    </xsl:if>
</xsl:template>

</xsl:stylesheet>
<!-- vim: set et ts=4 sw=4: -->
