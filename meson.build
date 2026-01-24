#
# Copyright (c) 2020 Jiri Svoboda
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
# - Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
# - The name of the author may not be used to endorse or promote products
#   derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
# IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
# OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
# INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
# NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#


c_args = [
  '-Wno-discarded-qualifiers'
]



deps = [ 'http', 'uri', 'lexbor' , 'console','inet','ui', 'gfx',
 'gfximage', 'gfxfont', 'posix', 'json', 'quickjs','mbedtls','lua']
src = files(
	'main.c',
	'css_parser.c',
	'js_executor_quickjs.c',
	'event_handler.c',
	'layout_engine.c',
	'forms_parser.c',
	'tables_parser.c',
	'lists_parser.c',
	'menus_parser.c',
	'img_parser.c',
	'media_parser.c',
	'link_parsing.c',
	'headings_parser.c',
	'iframe_parsing.c',
	'render_output.c',
	'layout_calculator.c',
	'lua_position.c',
	'position_layout.c',
	'gui.c',
	
)


if install_nonessential_data
	# TODO: install this file somewhere else eg in font dir
	installed_data += { 'name': 'text.html', 'dir': '/' }
	installed_data += { 'name': 'text1.html', 'dir': '/' }
	installed_data += { 'name': 'slika.html', 'dir': '/' }
	installed_data += { 'name': 'script.html', 'dir': '/' }
	installed_data += { 'name': 'test.xml', 'dir': '/' }
	installed_data += { 'name': 'test.svg', 'dir': '/' }
	installed_data += { 'name': 'position_calculator.lua', 'dir': '/' }
	installed_data += { 'name': 'position_calculator_final.lua', 'dir': '/' }

endif
