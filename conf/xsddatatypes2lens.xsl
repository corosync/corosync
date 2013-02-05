<?xml version="1.0" encoding="utf-8"?>
<xsl:stylesheet version="1.0"
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
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

<!-- xsltdoc (http://www.kanzaki.com/parts/xsltdoc.xsl) header -->
<xsl:template name="_doas_description">
    <rdf:RDF xmlns="http://purl.org/net/ns/doas#">
        <rdf:Description rdf:about="">
            <title>xsddatatypes2lens.xsl XSLT stylesheet</title>
            <description>
                This stylesheet contains a helper function-like template
                xsddatatype2lens.
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
            <!-- acknowledgement>
                XY added mapping for A and B.
            </acknowledgement -->
        </rdf:Description>
    </rdf:RDF>
</xsl:template>

<xsl:template name="xsddatatype2lens">
    <!--** Maps some XSD datatypes to respective primitives in Augeas lenses.

        Note that there is a simple support for backward compatibility
        through the compat-date parameter that, when explicitly set
        as suitable (YYYYMMDD format), will (usually) bring more
        fine-grained view for some datatypes.
    -->
    <xsl:param name="type"/>
    <xsl:param name="compat-date" select="0"/>
    <xsl:choose>
        <!--
            3.3 Primitive Datatypes
         -->
        <!-- TODO: string -->
        <!-- TODO: boolean -->
        <xsl:when test="$type = 'decimal'">Rx.reldecimal</xsl:when>
        <xsl:when test="$type = 'float'">Rx.reldecimal</xsl:when>
        <xsl:when test="$type = 'double'">Rx.reldecimal</xsl:when>
        <!-- TODO: duration -->
        <!-- TODO: dateTime -->
        <!-- TODO: time -->
        <!-- TODO: date -->
        <!-- TODO: gYearMonth -->
        <!-- TODO: gYear -->
        <!-- TODO: gMonthDay -->
        <!-- TODO: gDay -->
        <!-- TODO: gMonth -->
        <xsl:when test="$type = 'hexBinary'">Rx.hex</xsl:when>
        <!-- TODO: base64Binary -->
        <!-- TODO: anyURI -->
        <!-- TODO: QName -->
        <!-- TODO: NOTATION -->
        <!--
            3.4 Other Built-in Datatypes
         -->
        <!-- TODO: normalizedString -->
        <!-- TODO: token -->
        <!-- TODO: language -->
        <xsl:when test="$type = 'NMTOKEN'">Xml.nmtoken</xsl:when>
        <!-- TODO: NMTOKENS -->
        <!-- TODO: Name -->
        <!-- TODO: NCName -->
        <!-- TODO: ID -->
        <!-- TODO: IDREF -->
        <!-- TODO: IDREFS -->
        <!-- TODO: ENTITY -->
        <!-- TODO: ENTITIES -->
        <xsl:when test="$type = 'integer'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'nonPositiveInteger'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'negativeInteger'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'long'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'int'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'short'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'byte'">Rx.relinteger</xsl:when>
        <xsl:when test="$type = 'nonNegativeInteger'">Rx.integer</xsl:when>
        <xsl:when test="$type = 'unsignedLong'">Rx.integer</xsl:when>
        <xsl:when test="$type = 'unsignedInt'">Rx.integer</xsl:when>
        <xsl:when test="$type = 'unsignedShort'">Rx.integer</xsl:when>
        <xsl:when test="$type = 'unsignedByte'">
            <xsl:choose>
                <xsl:when test="$compat-date >= 20130103">
                    <xsl:text>Rx.byte</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>Rx.integer</xsl:text>
                </xsl:otherwise>
            </xsl:choose>
        </xsl:when>
        <xsl:when test="$type = 'positiveInteger'">Rx.integer</xsl:when>
        <!-- TODO: yearMonthDuration -->
        <!-- TODO: dayTimeDuration -->
        <!-- TODO: dateTimeStamp -->
        <xsl:otherwise>
            <xsl:message>
                <xsl:value-of select="concat('Unhandled XML datatype: ', $type)"/>
            </xsl:message>
        </xsl:otherwise>
    </xsl:choose>
</xsl:template>

</xsl:stylesheet>
<!-- vim: set et ts=4 sw=4: -->
