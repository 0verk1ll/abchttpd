#!/usr/bin/python

# SPDX-FileCopyrightText: 2021 2020 rdci and 0verk1ll
#
# SPDX-License-Identifier: mit

import os

print "post received"

file = open("posts", "a+")
file.write(os.environ['postdata']+"\r\n");

file.seek(0)
for l in file:
	print l
