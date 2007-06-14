# Copyright (c) 2004 MontaVista Software, Inc.
# 
# All rights reserved.
# 
# This software licensed under BSD license, the text of which follows:
# 
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
# 
# - Redistributions of source code must retain the above copyright notice,
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name of the MontaVista Software, Inc. nor the names of its
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

all: html
# The index.html file must be hand edited when adding manual pages

html:
	mkdir html
	groff -mandoc -Thtml openais_overview.8 > html/openais_overview.html
	groff -mandoc -Thtml openais.conf.5 > html/openais.conf.html
	groff -mandoc -Thtml evs_overview.8 > html/evs_overview.html
	groff -mandoc -Thtml evs_initialize.3 > html/evs_initialize.html
	groff -mandoc -Thtml evs_finalize.3 > html/evs_finalize.html
	groff -mandoc -Thtml evs_fd_get.3 > html/evs_fd_get.html
	groff -mandoc -Thtml evs_dispatch.3 > html/evs_dispatch.html
	groff -mandoc -Thtml evs_join.3 > html/evs_join.html
	groff -mandoc -Thtml evs_leave.3 > html/evs_leave.html
	groff -mandoc -Thtml evs_mcast_groups.3 > html/evs_mcast_groups.html
	groff -mandoc -Thtml evs_mcast_joined.3 > html/evs_mcast_joined.html
	groff -mandoc -Thtml evs_membership_get.3 > html/evs_membership_get.html

	groff -mandoc -Thtml cpg_overview.8 > html/cpg_overview.html
	groff -mandoc -Thtml cpg_initialize.3 > html/cpg_initialize.html
	groff -mandoc -Thtml cpg_finalize.3 > html/cpg_finalize.html
	groff -mandoc -Thtml cpg_fd_get.3 > html/cpg_fd_get.html
	groff -mandoc -Thtml cpg_dispatch.3 > html/cpg_dispatch.html
	groff -mandoc -Thtml cpg_join.3 > html/cpg_join.html
	groff -mandoc -Thtml cpg_leave.3 > html/cpg_leave.html
	groff -mandoc -Thtml cpg_mcast_joined.3 > html/cpg_mcast_joined.html
	groff -mandoc -Thtml cpg_membership_get.3 > html/cpg_membership_get.html
	groff -mandoc -Thtml cpg_context_get.3 > html/cpg_context_get.html
	groff -mandoc -Thtml cpg_context_set.3 > html/cpg_context_set.html

	cp index.html html

clean:
	rm -rf html

