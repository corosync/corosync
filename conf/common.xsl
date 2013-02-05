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
            <title>common.xsl XSLT stylesheet</title>
            <description>
                This stylesheet contains shared helper function-like
                templates.
            </description>
            <author rdf:parseType="Resource">
                <name>Jan Pokorný</name>
                <mbox rdf:resource="jpokorny@redhat.com"/>
            </author>
            <created>2013-02-04</created>
            <release rdf:parseType="Resource">
                <revision>0.1</revision>
                <created>2013-02-04</created>
            </release>
            <rights>Copyright 2013 Red Hat, Inc.</rights>
            <license rdf:resource="http://opensource.org/licenses/BSD-3-Clause"/>
        </rdf:Description>
    </rdf:RDF>
</xsl:template>

<xsl:template name="text-join">
    <!--** Simple text join of 'items' using 'sep' (defaults to space). -->
    <!-- TODO: normalize-space? -->
    <xsl:param name="items"/>
    <xsl:param name="sep" select="$SP"/>
    <xsl:param name="markup" value="''"/>
    <xsl:for-each select="$items">
        <xsl:choose>
            <xsl:when test="position() = 1">
                <xsl:value-of select="concat($markup, ., $markup)"/>
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="concat($sep, $markup, ., $markup)"/>
            </xsl:otherwise>
        </xsl:choose>
    </xsl:for-each>
</xsl:template>

</xsl:stylesheet>
