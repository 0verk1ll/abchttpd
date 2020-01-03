#!/usr/bin/python
import os

print "post received"

file = open("posts", "a+")
file.write(os.environ['postdata']+"\r\n");

file.seek(0)
for l in file:
	print l
